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


class PspMeItemRenderStreamContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.items_h = (ROOT / "src/ItemManager.hpp").read_text(encoding="utf-8")
        cls.items = (ROOT / "src/ItemManager.cpp").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src/BulletManager.cpp").read_text(encoding="utf-8")
        cls.audio_h = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")

    def test_feature_is_independent_and_psp2000_plus_only(self):
        self.assertIn("PSP_ME_ITEM_RENDER_STREAM ?= 0", self.makefile)
        self.assertIn("-DTH07_PSP_ME_ITEM_RENDER_STREAM", self.makefile)
        self.assertIn(
            "PSP_ME_ITEM_RENDER_STREAM requires PSP_ME_RENDER_DIRECT_LIST=1",
            self.makefile,
        )
        self.assertIn(
            "PSP_ME_ITEM_RENDER_STREAM is PSP-2000+ only", self.makefile
        )
        self.assertIn("$(PSP_ME_ITEM_RENDER_STREAM)-", self.makefile)

    def test_item_abi_stays_canonical_and_sidecars_are_feature_owned(self):
        self.assertIn("sizeof(Item) == 648u", self.bullets)
        for token in (
            "pspMeItemSlotGenerations[kItemCapacity]",
            "pspMeItemRenderSin[kItemCapacity]",
            "pspMeItemRenderCos[kItemCapacity]",
            "pspMeItemPreparedSerial",
            "pspMeItemPreparedCount",
        ):
            self.assertIn(token, self.items_h)
        spawn = body(self.items, "Item *ItemManager::SpawnItem(")
        self.assertIn("++this->pspMeItemSlotGenerations[itemIndex]", spawn)

    def test_presentation_moves_only_after_authoritative_update_finishes(self):
        update = body(self.items, "void ItemManager::OnUpdate()")
        self.assertLess(
            update.index("if (itemAcquired)"),
            update.index("PspPrepareMeItemRenderStream();"),
        )
        prepare = body(
            self.items, "bool ItemManager::PspPrepareMeItemRenderStream()"
        )
        for token in (
            "PspApplyItemDrawPresentation(item)",
            "PspMeItemRenderSinCos(rotation",
            "PspIsItemSlotTracked",
            "pspMeItemSlotGenerations[slot]",
            "this->listTail != last",
        ):
            self.assertIn(token, prepare)
        draw = body(self.items, "void ItemManager::OnDraw()")
        self.assertIn("PspMeItemRenderStreamPrepared()", draw)
        self.assertIn("g_AnmManager->Draw(&item->sprite)", draw)

    def test_item_layout_is_direct_list_and_has_complete_identity(self):
        for token in (
            "TH07_PSP_ME_RENDER_STREAM_JOB_ITEM_LIST",
            "typedef struct Th07PspMeRenderItemLayout",
            "itemBasePhys",
            "generationBasePhys",
            "activeBitsPhys",
            "sinBasePhys",
            "cosBasePhys",
            "headPhys",
            "tailPhys",
            "prepareSerialPhys",
            "preparedSerialPhys",
            "preparedCountPhys",
            "expectedPrepareSerial",
            "expectedItemCount",
        ):
            self.assertIn(token, self.audio_h)
        self.assertIn("__builtin_offsetof(AnmVm, pos.x) == 456u", self.bullets)
        self.assertIn("ME_RENDER_ITEM_VM_POS_X_OFFSET = 456", self.audio)

    def test_item_result_is_segment_local(self):
        for token in (
            "itemResult",
            "itemRecordCount",
            "itemVertexCount",
            "itemRunCount",
        ):
            self.assertIn(token, self.audio_h)
        worker = body(self.audio, "process_render_stream_on_me(")
        self.assertRegex(
            worker,
            r"itemResult[\s\S]*me_render_stream_expand_kernel",
        )
        # Bullet completion authority stays the top-level result. Item failure
        # publishes zero Item prefix, then the Bullet kernel still runs.
        self.assertIn("box->renderStreamResult = result", worker)
        self.assertIn("box->renderStreamItemResult = itemResult", worker)
        self.assertLess(
            worker.index("itemResult = me_render_stream_expand_kernel"),
            worker.index("uint32_t result = me_render_stream_expand_kernel"),
        )
        self.assertIn("itemVertexCount = 0u", worker)
        self.assertIn("itemRunCount = 0u", worker)

    def test_early_probe_preserves_item_loop_overlap(self):
        draw = body(self.bullets, "u32 BulletManager::OnDraw(")
        early = draw.index("PspMeRenderTryEarlyItemRetire")
        canonical_item = draw.index("g_ItemManager.OnDraw();")
        deadline = draw.index("PspMeRenderDrawDeadline();")
        self.assertLess(early, canonical_item)
        self.assertLess(canonical_item, deadline)
        self.assertIn("if (!pspMeItemConsumed)", draw)

    def test_item_reject_never_releases_bullet_ready_stream(self):
        consume = body(self.bullets, "bool PspMeRenderTryGeConsumeItem(")
        self.assertIn("streamItemFallback", consume)
        self.assertNotIn("PspMeRenderReleaseReadyForFallback", consume)
        self.assertNotIn("th07_psp_me_render_stream_release_ready", consume)

    def test_worker_forces_item_quads_but_resets_bullet_primitive_latch(self):
        self.assertIn("me_render_stream_reconstruct_item_record", self.audio)
        kernel = body(self.audio, "me_render_stream_expand_kernel(")
        self.assertRegex(kernel, r"generalMode\s*=\s*[\s\S]*itemList\s*\?\s*1")
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS", self.audio)

    def test_four_item_bullet_consume_combinations_keep_token_ownership(self):
        item = body(self.bullets, "bool PspMeRenderTryGeConsumeItem(")
        bullet = body(self.bullets, "bool PspMeRenderTryGeConsume(")
        after_item = self.bullets[
            self.bullets.index("bool PspMeRenderTryGeConsumeItem(") :
        ]
        reuse = body(
            after_item, "bool PspMeRenderReusePrefixGeValidation("
        )

        # Item ME + Bullet ME: one token submission is opened by Item and the
        # Bullet suffix resumes at the segment offsets without a second Begin.
        self.assertIn("state.prefixGeSubmissionOpen = true", item)
        self.assertIn("firstBulletRun", bullet)
        self.assertIn("state.ready.itemRunCount", bullet)
        self.assertRegex(
            bullet,
            r"!state\.prefixGeSubmissionOpen[\s\S]*"
            r"Th07PspBeginMeRenderGeSubmission",
        )
        # Begin changes READY_SC to GE_IN_FLIGHT.  The Bullet suffix must
        # reuse the exact Item-time validation authority instead of calling
        # ready_view_matches again (which correctly accepts READY_SC only).
        for evidence in (
            "state.prefixValidatedTokenSlot = state.token.slot",
            "state.prefixValidatedTokenGeneration = state.token.generation",
            "state.prefixValidatedDrawSeq = expectedDrawSeq",
        ):
            self.assertIn(evidence, item)
        self.assertIn("PspMeRenderReusePrefixGeValidation", bullet)
        self.assertIn("state.prefixGeSubmissionOpen", bullet)
        self.assertIn("PspMeRenderReadyAuthorityMatches(expectedDrawSeq)", reuse)
        self.assertNotIn(
            "th07_psp_me_render_stream_ready_view_matches", reuse
        )
        for evidence in (
            "state.prefixValidatedTokenSlot != state.token.slot",
            "state.prefixValidatedTokenGeneration != state.token.generation",
            "state.prefixValidatedDrawSeq != expectedDrawSeq",
            "state.ready.token.slot != state.token.slot",
            "state.ready.token.generation != state.token.generation",
            "state.job.targetDrawSeq != expectedDrawSeq",
        ):
            self.assertIn(evidence, reuse)

        # Item ME + Bullet canonical: once Item commands are visible, Bullet
        # authority failure closes GE ownership and never releases READY_SC.
        mismatch = bullet[
            bullet.index("if (!streamValidated)") :
            bullet.index("const u32 records")
        ]
        self.assertIn("if (state.prefixGeSubmissionOpen)", mismatch)
        self.assertIn("Th07PspEndMeRenderGeSubmission()", mismatch)
        self.assertNotIn("PspMeRenderReleaseReadyForFallback(true)",
                         mismatch.split("return false;")[0])

        # Item canonical + Bullet ME: Item begin/reject never frees the token;
        # Bullet may start ownership later and consumes runs whose firstVertex
        # already includes the unpublished Item arena prefix.
        self.assertNotIn("th07_psp_me_render_stream_release_ready", item)
        self.assertIn("run.firstVertex", bullet)
        self.assertIn("state.ready.vertices + run.firstVertex", bullet)

        # Item canonical + Bullet canonical: only the Bullet fallback owns the
        # READY release, exactly once, when no Item GE submission was opened.
        self.assertIn("PspMeRenderReleaseReadyForFallback(true)", bullet)
        self.assertIn("PspMeRenderReleaseReadyForFallback(false)", bullet)

    def test_item_mailbox_publish_and_ready_echo_are_complete(self):
        for token in (
            "box->renderStreamItemResult = itemResult",
            "box->renderStreamItemRecordCount = itemRecordCount",
            "box->renderStreamItemVertexCount = itemVertexCount",
            "box->renderStreamItemRunCount = itemRunCount",
            "completion->itemResult = box->renderStreamItemResult",
            "ready->itemResult = local.itemResult",
            "me_render_stream_item_completion_valid",
            "ME_RENDER_STREAM_POOL_MAX_RUNS",
        ):
            self.assertIn(token, self.audio)

    def test_boot_selftest_proves_prefix_success_and_local_failure(self):
        direct = body(self.audio, "selftest_render_stream_direct_list(")
        for token in (
            "me_render_item_selftest_layout",
            "itemCompletion.itemVertexCount != 4u",
            "itemReady.runs[1].firstVertex != 4u",
            "itemRejectCompletion.result !=",
            "itemRejectCompletion.itemResult !=",
            "itemRejectCompletion.itemVertexCount != 0u",
            "itemRejectReady.runs[0].firstVertex != 0u",
        ):
            self.assertIn(token, direct)


if __name__ == "__main__":
    unittest.main()

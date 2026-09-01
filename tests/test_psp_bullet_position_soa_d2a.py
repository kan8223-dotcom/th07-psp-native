from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "PspBulletPositionSoa.hpp"
HARNESS = ROOT / "tests" / "psp_bullet_position_soa_d2a_harness.cpp"
MAKEFILE = ROOT / "Makefile"
BULLETS = ROOT / "src" / "BulletManager.cpp"
BULLETS_H = ROOT / "src" / "BulletManager.hpp"
GAME = ROOT / "src" / "GameManager.cpp"
REPLAY = ROOT / "src" / "ReplayManager.cpp"
GRAPHICS = ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"


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
    raise AssertionError(f"unterminated function or branch: {signature}")


def make_target_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    next_target = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    if next_target is None:
        return makefile[start:]
    return makefile[start : start + len(target) + 1 + next_target.start()]


class PspBulletPositionSoaD2aTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = MAKEFILE.read_text(encoding="utf-8")
        cls.bullets = BULLETS.read_text(encoding="utf-8")
        cls.bullets_h = BULLETS_H.read_text(encoding="utf-8")
        cls.game = GAME.read_text(encoding="utf-8")
        cls.replay = REPLAY.read_text(encoding="utf-8")
        cls.graphics = GRAPHICS.read_text(encoding="utf-8")

    def compile_and_run(self, optimization: str) -> subprocess.CompletedProcess[str]:
        compiler = shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("host C++ compiler is unavailable")
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "d2a-position-soa"
            build = subprocess.run(
                [
                    compiler,
                    "-std=gnu++17",
                    optimization,
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    str(HARNESS),
                    "-o",
                    str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run(
                [str(output)], check=False, capture_output=True, text=True
            )

    def test_header_contract_is_explicit_and_self_contained(self) -> None:
        text = HEADER.read_text(encoding="utf-8")
        for contract in (
            "TH07_PSP_BULLET_POSITION_SOA_CAPACITY = 1024u",
            "TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE = 1040u",
            "validBits[TH07_PSP_BULLET_POSITION_SOA_VALID_WORDS]",
            "generation[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE]",
            "publishManagerSerial[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE]",
            "publishCalcSerial[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE]",
            "posXBits[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE]",
            "posYBits[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE]",
            "posZBits[TH07_PSP_BULLET_POSITION_SOA_PLANE_STRIDE]",
            "managerSerial",
            "activeCalcSerial",
            "PublishRaw",
            "ValidateRaw",
            "InvalidateAll",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, text)

    def test_makefile_gate_is_default_off_fail_closed_and_profile_stamped(self) -> None:
        feature = "PSP_BULLET_POSITION_SOA_SHADOW"
        self.assertIn(f"{feature} ?= 0", self.makefile)
        self.assertIn(
            f"ifneq ($(filter-out 0 1,$({feature})),)", self.makefile
        )

        gate_start = self.makefile.index(f"ifeq ($({feature}),1)")
        gate_end = self.makefile.index(
            "ifneq ($(filter 1,$(PSP_ME_BULLET_OUTPUT_SLIM)", gate_start
        )
        gate = self.makefile[gate_start:gate_end]
        for contract in (
            "PSP_ME_BULLET_COMPACT_UPDATE",
            "PSP_ME_RENDER_CORRECTNESS",
            "PSP_PERF_AB_COMPARE",
            "PSP_1000",
            "-DTH07_PSP_BULLET_POSITION_SOA_SHADOW",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, gate)

        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({feature})", stamp)

    def test_d2a_target_isolated_from_d1_c1_c2_and_effect(self) -> None:
        target = make_target_body(
            self.makefile, "psp3000-a6v4w-d2a-position-soa-shadow-build"
        )
        for assignment in (
            "PSP_1000=0",
            "PSP_RID30_AB_ME_UV16=0",
            "PSP_RID30_AB_ME_XYZ16=0",
            "PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0",
            "PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0",
            "PSP_RID30_AB_ME_SEED_SOA=0",
            "PSP_RID30_AB_ME_POSITION_SOA_SHADOW=1",
        ):
            with self.subTest(assignment=assignment):
                self.assertIn(assignment, target)
        self.assertIn("psp3000-rid30-ab-me-build", target)

        inherited = make_target_body(
            self.makefile, "psp3000-rid30-ab-me-build"
        )
        for assignment in (
            "PSP_ME_EFFECT_RENDER_STREAM=0",
            "PSP_ME_BULLET_OUTPUT_SLIM=0",
            "PSP_ME_BULLET_SEED_SLIM=0",
            "PSP_ME_ITEM_SEED_SLIM=0",
        ):
            with self.subTest(inherited_assignment=assignment):
                self.assertIn(assignment, inherited)

    def test_initialize_resets_manager_identity_after_storage_reset(self) -> None:
        initialize = function_body(self.bullets, "void BulletManager::Initialize()")
        storage_reset = initialize.index(
            "memset(this, 0, sizeof(BulletManager));"
        )
        d2a_reset = initialize.index("PspBulletPositionSoaResetManager();")
        initialized_field = initialize.index("this->itemType = ITEM_POINT_BULLET;")
        self.assertLess(storage_reset, d2a_reset)
        self.assertLess(d2a_reset, initialized_field)
        self.assertEqual(initialize.count("PspBulletPositionSoaResetManager();"), 1)

    def test_track_and_forget_invalidate_after_generation_change(self) -> None:
        for signature, active_bit_update in (
            (
                "void PspTrackBulletSlot(i32 index)",
                "pspActiveBulletBits[index >> 5] |= 1u << (index & 31);",
            ),
            (
                "void PspForgetBulletSlot(i32 index)",
                "pspActiveBulletBits[index >> 5] &= ~(1u << (index & 31));",
            ),
        ):
            with self.subTest(signature=signature):
                slot_hook = function_body(self.bullets_h, signature)
                active = slot_hook.index(active_bit_update)
                generation = slot_hook.index("++pspMeRenderSlotGenerations[index]")
                invalidate = slot_hook.index(
                    "Th07PspBulletPositionSoaInvalidateSlot(static_cast<u32>(index));"
                )
                self.assertLess(active, generation)
                self.assertLess(generation, invalidate)
                self.assertEqual(
                    slot_hook.count("Th07PspBulletPositionSoaInvalidateSlot"), 1
                )
                self.assertNotIn("PspBulletPositionSoaPublishSlot", slot_hook)

    def test_deferred_eligible_bitmap_tracks_publish_and_all_invalidation_paths(
        self,
    ) -> None:
        runtime = function_body(self.bullets, "struct PspBulletPositionSoaRuntime")
        self.assertIn(
            "u32 deferredEligibleBits[TH07_PSP_BULLET_POSITION_SOA_VALID_WORDS];",
            runtime,
        )

        setter = function_body(
            self.bullets, "inline void PspBulletPositionSoaSetDeferred("
        )
        self.assertIn("if (deferred)", setter)
        self.assertIn("word |= mask;", setter)
        self.assertIn("word &= ~mask;", setter)

        publish = function_body(
            self.bullets, "void PspBulletPositionSoaPublishSlot("
        )
        self.assertIn(
            "void PspBulletPositionSoaPublishSlot(BulletManager *manager,\n"
            "                                     Bullet *bullet, u32 slot,\n"
            "                                     bool spawnPublish,\n"
            "                                     bool deferredEligible)",
            self.bullets,
        )
        shadow_publish = publish.index("runtime.shadow.Publish(")
        rejected_clear = publish.index(
            "PspBulletPositionSoaInvalidateRuntimeSlot(slot);", shadow_publish
        )
        final_eligible = publish.index(
            "const bool finalDeferredEligible =\n"
            "        deferredEligible && PspMeBulletFastIsEligible(bullet);",
            rejected_clear,
        )
        eligible_set = publish.index(
            "PspBulletPositionSoaSetDeferred(slot, finalDeferredEligible);",
            final_eligible,
        )
        accepted_count = publish.index("++window.publishes;", eligible_set)
        self.assertLess(shadow_publish, rejected_clear)
        self.assertLess(rejected_clear, final_eligible)
        self.assertLess(final_eligible, eligible_set)
        self.assertLess(eligible_set, accepted_count)

        slot_clear = function_body(
            self.bullets,
            "inline void PspBulletPositionSoaInvalidateRuntimeSlot(",
        )
        self.assertLess(
            slot_clear.index("shadow.Invalidate(slot);"),
            slot_clear.index("PspBulletPositionSoaSetDeferred(slot, false);"),
        )
        all_clear = function_body(
            self.bullets,
            "inline void PspBulletPositionSoaInvalidateRuntimeAll()",
        )
        self.assertLess(
            all_clear.index("shadow.InvalidateAll();"),
            all_clear.index("memset(gPspBulletPositionSoa.deferredEligibleBits, 0"),
        )
        reset = function_body(
            self.bullets, "void PspBulletPositionSoaResetManager()"
        )
        self.assertLess(
            reset.index("runtime.shadow.Reset(runtime.managerSerial, 1u);"),
            reset.index("memset(runtime.deferredEligibleBits, 0"),
        )

        slot_boundaries = (
            "bool PspBulletPositionSoaObserveSlot(",
            "void PspBulletPositionSoaPublishSlot(",
            "void PspBulletPositionSoaObserveMutation(",
            "void Th07PspBulletPositionSoaInvalidateSlot(",
        )
        for signature in slot_boundaries:
            with self.subTest(slot_boundary=signature):
                boundary = function_body(self.bullets, signature)
                self.assertIn(
                    "PspBulletPositionSoaInvalidateRuntimeSlot(slot);", boundary
                )

        all_boundaries = (
            "void PspBulletPositionSoaPauseBoundary()",
            "void Th07PspBulletPositionSoaDemoRestartBoundary()",
        )
        for signature in all_boundaries:
            with self.subTest(all_boundary=signature):
                boundary = function_body(self.bullets, signature)
                self.assertIn("PspBulletPositionSoaInvalidateRuntimeAll();", boundary)

        self.assertEqual(self.bullets.count(".shadow.Invalidate(slot);"), 1)
        self.assertEqual(self.bullets.count(".shadow.InvalidateAll();"), 1)

        count_deferred = function_body(
            self.bullets, "inline u32 PspBulletPositionSoaCountDeferred()"
        )
        self.assertIn(
            "word < TH07_PSP_BULLET_POSITION_SOA_VALID_WORDS", count_deferred
        )
        self.assertIn("bits &= bits - 1u;", count_deferred)
        self.assertIn("++count;", count_deferred)

        pause = function_body(
            self.bullets, "void PspBulletPositionSoaPauseBoundary()"
        )
        pause_count = pause.index(
            "gPspBulletPositionSoaWindow.wouldMaterializePause +=\n"
            "            PspBulletPositionSoaCountDeferred();"
        )
        pause_clear = pause.index(
            "PspBulletPositionSoaInvalidateRuntimeAll();", pause_count
        )
        self.assertLess(pause_count, pause_clear)

        demo = function_body(
            self.bullets, "void Th07PspBulletPositionSoaDemoRestartBoundary()"
        )
        demo_count = demo.index(
            "gPspBulletPositionSoaWindow.wouldMaterializeDemoRestart +=\n"
            "        PspBulletPositionSoaCountDeferred();"
        )
        demo_clear = demo.index(
            "PspBulletPositionSoaInvalidateRuntimeAll();", demo_count
        )
        self.assertLess(demo_count, demo_clear)

    def test_observe_mutation_match_keeps_shadow_and_fault_drops_full_slot(
        self,
    ) -> None:
        mutation = function_body(
            self.bullets, "void PspBulletPositionSoaObserveMutation("
        )
        post_classification = mutation[mutation.index("++window.mutationFaults;") :]

        matched = function_body(
            post_classification,
            "if (result == TH07_PSP_BULLET_POSITION_SOA_MATCH)",
        )
        self.assertIn("PspBulletPositionSoaSetDeferred(slot, false);", matched)
        self.assertNotIn("PspBulletPositionSoaInvalidateRuntimeSlot", matched)
        self.assertNotIn("++window.invalidations", matched)

        fault = function_body(
            post_classification,
            "else if (result != TH07_PSP_BULLET_POSITION_SOA_NOT_VALID &&\n"
            "             result != TH07_PSP_BULLET_POSITION_SOA_NOT_INITIALIZED)",
        )
        invalidate = fault.index("PspBulletPositionSoaInvalidateRuntimeSlot(slot);")
        invalidation_count = fault.index("++window.invalidations;", invalidate)
        self.assertLess(invalidate, invalidation_count)
        self.assertNotIn("PspBulletPositionSoaSetDeferred(slot, false);", fault)

        self.assertEqual(
            post_classification.count("PspBulletPositionSoaSetDeferred(slot, false);"),
            1,
        )
        self.assertEqual(
            post_classification.count("PspBulletPositionSoaInvalidateRuntimeSlot(slot);"),
            1,
        )

    def test_spawn_publishes_only_after_commands_and_screen_clear(self) -> None:
        spawn = function_body(
            self.bullets, "i32 BulletManager::SpawnSingleBullet("
        )
        commands = spawn.rindex("bullet->RunCommands();")
        screen_clear = spawn.index("if (this->screenClearTime != 0", commands)
        despawn = spawn.index("bullet->state = BULLET_DESPAWN;", screen_clear)
        publish = spawn.index("PspBulletPositionSoaPublishSlot(", despawn)
        cursor_advance = spawn.index("bulletIndex++;", publish)
        self.assertLess(commands, screen_clear)
        self.assertLess(screen_clear, despawn)
        self.assertLess(despawn, publish)
        self.assertLess(publish, cursor_advance)
        self.assertEqual(spawn.count("PspBulletPositionSoaPublishSlot("), 1)
        self.assertIn(
            "this, bullet, static_cast<u32>(bulletIndex), true, false",
            spawn[publish:],
        )

    def test_update_observes_before_mutation_and_publishes_at_final_timers(self) -> None:
        update = function_body(self.bullets, "u32 BulletManager::OnUpdate(")
        loop = update.index("for (i = 0; i < kBulletCapacity; i++)")
        inactive = update.index("if (bullet->state == BULLET_INACTIVE)", loop)
        observe = update.index("PspBulletPositionSoaObserveSlot(", inactive)
        latch_reset = update.rindex(
            "pspBulletPositionSoaWouldDefer = false;", loop, inactive
        )
        latch_assign = update.rindex(
            "pspBulletPositionSoaWouldDefer = ", inactive, observe
        )
        count = update.index("arg->bulletCount++;", observe)
        state_switch = update.index("switch (bullet->state)", count)
        commands = update.index("bullet->RunCommands();", state_switch)
        self.assertLess(latch_reset, inactive)
        self.assertLess(inactive, latch_assign)
        self.assertLess(latch_assign, observe)
        self.assertLess(observe, count)
        self.assertLess(count, state_switch)
        self.assertLess(state_switch, commands)

        timers = update.index("update_timers:", commands)
        timer1 = update.index("bullet->timer1++;", timers)
        timer2 = update.index("bullet->timer2++;", timer1)
        publish = update.index("PspBulletPositionSoaPublishSlot(", timer2)
        list_link = update.index("bullet->next = arg->bulletsPtrs", publish)
        self.assertLess(timers, timer1)
        self.assertLess(timer1, timer2)
        self.assertLess(timer2, publish)
        self.assertLess(publish, list_link)
        self.assertEqual(update.count("PspBulletPositionSoaPublishSlot("), 1)
        self.assertIn(
            "arg, bullet, static_cast<u32>(blockIdx), false,\n"
            "            pspBulletPositionSoaWouldDefer",
            update[publish:],
        )

    def test_bulk_reads_are_captured_before_mutation_and_destructive_writes(
        self,
    ) -> None:
        cases = (
            (
                "void BulletManager::RemoveAllBullets(",
                (
                    "memset(bullet, 0, sizeof(Bullet));",
                    "bullet->state = BULLET_DESPAWN;",
                ),
            ),
            (
                "i32 BulletManager::DespawnBullets(",
                (
                    "bullet->state = BULLET_DESPAWN;",
                ),
            ),
            (
                "void BulletManager::RemoveBulletsInRadius(",
                (
                    "memset(bullet, 0, sizeof(Bullet));",
                ),
            ),
        )
        for signature, destructive_writes in cases:
            with self.subTest(signature=signature):
                mutation = function_body(self.bullets, signature)
                capture = mutation.index(
                    "ZunVec3 *bulletPosition = &bullet->pos;"
                )
                observe = mutation.index("PspBulletPositionSoaObserveMutation(")
                self.assertLess(capture, observe)
                self.assertEqual(
                    mutation.count("PspBulletPositionSoaObserveMutation("), 1
                )
                self.assertIn("PspReadBulletPosition(", mutation[:observe])
                self.assertIn("bulletPosition", mutation[observe:])
                for operation in destructive_writes:
                    with self.subTest(operation=operation):
                        self.assertLess(observe, mutation.index(operation))

    def test_bulk_mutation_reasons_are_exhaustive_and_assigned_by_path(self) -> None:
        reasons = function_body(
            self.bullets, "enum PspBulletPositionSoaMutationReason"
        )
        expected_reasons = (
            "PSP_BULLET_POSITION_SOA_MUTATION_BULK_CLEAR_ITEM = 0",
            "PSP_BULLET_POSITION_SOA_MUTATION_DESPAWN_TRANSITION = 1",
            "PSP_BULLET_POSITION_SOA_MUTATION_BULK_DESPAWN = 2",
            "PSP_BULLET_POSITION_SOA_MUTATION_RADIUS_QUERY = 3",
        )
        for reason in expected_reasons:
            with self.subTest(reason=reason):
                self.assertIn(reason, reasons)
        self.assertEqual(reasons.count("PSP_BULLET_POSITION_SOA_MUTATION_"), 4)

        remove_all = function_body(
            self.bullets, "void BulletManager::RemoveAllBullets("
        )
        observe = remove_all.index("PspBulletPositionSoaObserveMutation(")
        observe_end = remove_all.index("#endif", observe)
        remove_all_observe = remove_all[observe:observe_end]
        self.assertIn(
            "(param_1 != 0 && param_1 < 9)\n"
            "                ? PSP_BULLET_POSITION_SOA_MUTATION_BULK_CLEAR_ITEM\n"
            "                : PSP_BULLET_POSITION_SOA_MUTATION_DESPAWN_TRANSITION",
            remove_all_observe,
        )

        assigned = (
            (
                "i32 BulletManager::DespawnBullets(",
                "PSP_BULLET_POSITION_SOA_MUTATION_BULK_DESPAWN",
            ),
            (
                "void BulletManager::RemoveBulletsInRadius(",
                "PSP_BULLET_POSITION_SOA_MUTATION_RADIUS_QUERY",
            ),
        )
        for signature, expected_reason in assigned:
            with self.subTest(signature=signature):
                mutation = function_body(self.bullets, signature)
                observe = mutation.index("PspBulletPositionSoaObserveMutation(")
                observe_end = mutation.index("#endif", observe)
                observe_call = mutation[observe:observe_end]
                self.assertIn(expected_reason, observe_call)
                for other_reason in expected_reasons:
                    other_name = other_reason.split(" =", 1)[0]
                    if other_name != expected_reason:
                        self.assertNotIn(other_name, observe_call)

    def test_game_manager_pause_and_retry_break_clear_the_shadow(self) -> None:
        update = function_body(self.game, "u32 GameManager::OnUpdate(")
        pause_branch = function_body(
            update,
            "if (arg->isInPauseMenu == 1 || arg->isInPauseMenu == 2 || "
            "arg->isInRetryMenu)",
        )
        boundary = pause_branch.index("Th07PspBulletPositionSoaPauseBoundary();")
        chain_break = pause_branch.index("return CHAIN_CALLBACK_RESULT_BREAK;")
        self.assertLess(boundary, chain_break)
        self.assertEqual(pause_branch.count("Th07PspBulletPositionSoaPauseBoundary"), 1)

    def test_both_demo_restart_branches_clear_before_restarting(self) -> None:
        update = function_body(
            self.replay, "u32 ReplayManager::OnUpdateDemoLowPrio("
        )
        conditions = (
            "if (g_Gui.HasCurrentMsgIdx() && g_Gui.IsDialogueSkippable() && "
            "arg->frameId % 3 != 2)",
            "if (g_GameManager.replayStage == 2 && "
            "!g_EnemyManager.HasActiveBoss() && arg->frameId % 5 != 4)",
        )
        for condition in conditions:
            with self.subTest(condition=condition):
                branch = function_body(update, condition)
                boundary = branch.index(
                    "Th07PspBulletPositionSoaDemoRestartBoundary();"
                )
                restart = branch.index(
                    "return CHAIN_CALLBACK_RESULT_RESTART_FROM_FIRST_JOB;"
                )
                self.assertLess(boundary, restart)
        self.assertEqual(
            update.count("Th07PspBulletPositionSoaDemoRestartBoundary();"), 2
        )
        self.assertEqual(
            update.count("return CHAIN_CALLBACK_RESULT_RESTART_FROM_FIRST_JOB;"),
            2,
        )

    def test_perf_accept_closes_coverage_and_emits_all_d2a_tokens(self) -> None:
        report = function_body(self.graphics, "void ReportPerfWindow(")
        self.assertEqual(
            report.count("Th07PspTakeBulletPositionSoaWindow(&abPositionSoa);"),
            1,
        )
        for closure in (
            "abPositionSoaClassified == abPositionSoa.activeVisits",
            "abPositionSoa.unsupportedMatches ==\n"
            "                abPositionSoa.matches",
            "abPositionSoa.wouldMaterializeUnsupported <=\n"
            "                abPositionSoa.unsupportedMatches +\n"
            "                    abPositionSoa.wouldDefer",
            "abPositionSoa.mutationNotValid +\n"
            "                    abPositionSoa.mutationFaults ==\n"
            "                abPositionSoa.mutationVisits",
            "abPositionSoa.mutationDeferred +\n"
            "                    abPositionSoa.mutationCanonical ==\n"
            "                abPositionSoa.mutationMatches",
            "abPositionSoa.mutationRadiusQuery ==\n"
            "                abPositionSoa.mutationDeferred",
            "abPositionSoa.spawnPublishes <= abPositionSoa.publishes",
            "abPositionSoa.validSlots <=\n"
            "                static_cast<unsigned int>(BulletManager::kBulletCapacity)",
            "abPositionSoa.managerMismatch == 0u",
            "abPositionSoa.generationMismatch == 0u",
            "abPositionSoa.calcMismatch == 0u",
            "abPositionSoa.positionMismatch == 0u",
            "abPositionSoa.invalidSlot == 0u",
            "abPositionSoa.publishRejected == 0u",
            "abPositionSoa.mutationFaults == 0u",
            "acceptProfileValid = acceptProfileValid && abPositionSoaValid;",
        ):
            with self.subTest(closure=closure):
                self.assertIn(closure, report)

        token_format = (
            '"PSV%llu PSM%llu PSC%llu PSWD%llu PSWN%llu PSWU%llu "\n'
            '            "PSP%llu PSS%llu PSI%llu "\n'
            '            "PSMV%llu PSMM%llu PSMC%llu PSMD%llu PSMK%llu PSMF%u "\n'
            '            "PSMR%llu/%llu/%llu/%llu "\n'
            '            "PSX%u/%u/%u/%u/%u/%u "\n'
            '            "PSB%u/%u PSBM%llu/%llu PSR%u/%u PSVC%u PSG%u "'
        )
        self.assertIn(token_format, report)
        for field in (
            "abPositionSoa.activeVisits",
            "abPositionSoa.matches",
            "abPositionSoa.notValid",
            "abPositionSoa.wouldDefer",
            "abPositionSoa.unsupportedMatches",
            "abPositionSoa.wouldMaterializeUnsupported",
            "abPositionSoa.publishes",
            "abPositionSoa.spawnPublishes",
            "abPositionSoa.invalidations",
            "abPositionSoa.mutationVisits",
            "abPositionSoa.mutationMatches",
            "abPositionSoa.mutationNotValid",
            "abPositionSoa.mutationDeferred",
            "abPositionSoa.mutationCanonical",
            "abPositionSoa.mutationFaults",
            "abPositionSoa.mutationBulkClearItem",
            "abPositionSoa.mutationDespawnTransition",
            "abPositionSoa.mutationBulkDespawn",
            "abPositionSoa.mutationRadiusQuery",
            "abPositionSoa.pauseClears",
            "abPositionSoa.demoRestartClears",
            "abPositionSoa.wouldMaterializePause",
            "abPositionSoa.wouldMaterializeDemoRestart",
            "abPositionSoa.managerResets",
            "abPositionSoa.calcPasses",
            "abPositionSoa.validSlots",
            "abPositionSoaValid ? 1u : 0u",
        ):
            with self.subTest(field=field):
                self.assertIn(field, report)

    def test_harness_passes_without_optimizer(self) -> None:
        completed = self.compile_and_run("-O0")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("D2A position SoA", completed.stdout)

    def test_harness_passes_with_psp_hot_tu_optimization(self) -> None:
        completed = self.compile_and_run("-O3")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("raw bits, generation, serial fences", completed.stdout)


if __name__ == "__main__":
    unittest.main()

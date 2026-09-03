from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_1000_ENEMY_MANIFEST"
MACRO = "TH07_PSP_1000_ENEMY_MANIFEST"


def function_body(source: str, signature: str, start: int = 0) -> str:
    """Return a C/C++ function body without depending on source line numbers."""
    function = source.index(signature, start)
    opening = source.index("{", function)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def braced_block(source: str, marker: str, start: int = 0) -> str:
    marker_at = source.index(marker, start)
    opening = source.index("{", marker_at)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated block: {marker}")


def recipe_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    next_target = re.search(
        r"^[A-Za-z0-9_.-]+(?:\s+[A-Za-z0-9_.-]+)*\s*:", tail, re.MULTILINE
    )
    if next_target is None:
        return makefile[start:]
    return makefile[start : start + len(target) + 1 + next_target.start()]


def cpp_integer(source: str, name: str) -> int:
    match = re.search(
        rf"\b{name}\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)(?:[uUlL]+)?\s*;", source
    )
    if match is None:
        raise AssertionError(f"missing integer constant: {name}")
    return int(match.group(1), 0)


def cpp_integer_array(source: str, name: str) -> list[int]:
    match = re.search(
        rf"\b{name}\s*\[\s*[0-9]+\s*\]\s*=\s*\{{(.*?)\}}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing integer array: {name}")
    return [
        int(value, 0)
        for value in re.findall(r"0x[0-9a-fA-F]+|[0-9]+", match.group(1))
    ]


def allocation_calls(source: str) -> list[str]:
    return re.findall(
        r"\b(?:malloc|calloc|realloc|memalign|aligned_alloc)\s*\(|\bnew\s+",
        source,
    )


class Psp1000EnemyManifestSourceContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.enemy = (ROOT / "src" / "EnemyManager.cpp").read_text(encoding="utf-8")
        cls.enemy_h = (ROOT / "src" / "EnemyManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.bullet_h = (ROOT / "src" / "BulletManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.item_h = (ROOT / "src" / "ItemManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.effect_h = (ROOT / "src" / "EffectManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.ecl = (ROOT / "src" / "EclManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.enemy_ecl = (ROOT / "src" / "EnemyEclInstr.cpp").read_text(
            encoding="utf-8"
        )
        cls.game = (ROOT / "src" / "GameManager.cpp").read_text(encoding="utf-8")
        cls.replay = (ROOT / "src" / "ReplayManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.supervisor = (ROOT / "src" / "Supervisor.cpp").read_text(
            encoding="utf-8"
        )
        cls.text = (ROOT / "src" / "TextHelper.cpp").read_text(
            encoding="utf-8"
        )
        cls.arena = (ROOT / "psp" / "psp1000_arena.cpp").read_text(
            encoding="utf-8"
        )
        cls.game_added = function_body(cls.game, "ZunResult GameManager::AddedCallback")
        cls.game_deleted = function_body(
            cls.game, "ZunResult GameManager::DeletedCallback"
        )

    def test_feature_is_default_off_psp1000_only_and_profile_stamped(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_TITLE_ARCHIVE_WORKSPACE),1)", start)
        gate = self.makefile[start:end]
        for required in (
            "ifneq ($(PSP_1000),1)",
            f"{FEATURE} is PSP-1000 only",
            "ifneq ($(PSP_LOCAL_FONT_SUBSET),1)",
            f"{FEATURE} requires PSP_LOCAL_FONT_SUBSET=1",
            f"-D{MACRO}",
            f"{FEATURE} must be 0 or 1",
        ):
            with self.subTest(required=required):
                self.assertIn(required, gate)

        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)

        capacity = self.enemy_h[
            self.enemy_h.index("static constexpr i32 kEnemyCapacity") :
            self.enemy_h.index("EnemyManager();")
        ]
        self.assertIn(f"defined({MACRO})", capacity)
        self.assertRegex(capacity, rf"defined\({MACRO}\)\s*\n\s*480\s*;")

    def test_dedicated_udluna_build_is_the_only_named_psp1000_opt_in(self) -> None:
        target = recipe_body(
            self.makefile, "psp1000-udluna-enemy-manifest-build"
        )
        for required in (
            "PSP_1000=1",
            "PSP_LOCAL_FONT_SUBSET=1",
            f"{FEATURE}=1",
            "psp1000-build",
        ):
            with self.subTest(required=required):
                self.assertIn(required, target)

        ordinary = recipe_body(self.makefile, "psp1000-build")
        self.assertNotIn(f"{FEATURE}=1", ordinary)
        self.assertIn(f"{FEATURE}=$({FEATURE})", ordinary)

    def test_udluna_identity_header_and_stage_tables_are_fully_pinned(self) -> None:
        self.assertEqual(cpp_integer(self.enemy, "kUdLunaRawFnv1a"), 0xDF8402D05977CFAA)
        self.assertEqual(cpp_integer(self.enemy, "kUdLunaRawBytes"), 72308)
        self.assertEqual(cpp_integer(self.enemy, "kUdLunaChecksum"), 0x3FA1D445)
        self.assertEqual(cpp_integer(self.enemy, "kUdLunaReplayBytes"), 510499)
        self.assertEqual(cpp_integer(self.enemy, "kUdLunaCompressedBytes"), 72224)
        self.assertEqual(cpp_integer(self.enemy, "kUdLunaPayloadBytes"), 510415)
        self.assertEqual(
            cpp_integer_array(self.enemy, "kUdLunaStageOffsets"),
            [232, 45700, 107292, 179140, 289776, 376912, 0],
        )
        self.assertEqual(
            cpp_integer_array(self.enemy, "kUdLunaStageEndOffsets"),
            [506276, 506656, 507170, 507770, 508693, 509420, 0],
        )
        self.assertRegex(
            self.enemy,
            r"(?i)D6B6634FB12DBA2DF5084D04DB05612FC681735DBC0D035A42A52143DFFB498F",
        )

        match = function_body(self.enemy, "bool PspReplayMatchesUdLuna")
        for required in (
            "rawFileBytes != kUdLunaRawBytes",
            "rawFileFnv1a != kUdLunaRawFnv1a",
            "head.checksum",
            "kUdLunaChecksum",
            "head.replaySize == kUdLunaReplayBytes",
            "head.compressedSize == kUdLunaCompressedBytes",
            "head.sizeWithoutHeader == kUdLunaPayloadBytes",
            "head.stageReplayDataOffsets",
            "kUdLunaStageOffsets",
            "head.stageEndDataOffsets",
            "kUdLunaStageEndOffsets",
        ):
            with self.subTest(required=required):
                self.assertIn(required, match)

        replay_added = function_body(
            self.replay, "ZunResult ReplayManager::AddedCallbackDemo"
        )
        loaded = replay_added.index("FileSystem::OpenFile")
        raw_size = replay_added.index("rawFileBytes", loaded)
        raw_hash = replay_added.index("PspReplayRawFnv1a", raw_size)
        validated = replay_added.index("ValidateReplayData", raw_hash)
        self.assertLess(loaded, raw_size)
        self.assertLess(raw_size, raw_hash)
        self.assertLess(raw_hash, validated)

    def test_partial_replay_registration_is_cleanup_safe_before_sync_callback(self) -> None:
        register = function_body(
            self.replay, "ZunResult ReplayManager::RegisterChain"
        )
        allocated = register.index("ReplayManager *mgr = new ReplayManager()")
        published = register.index("g_ReplayManager = mgr", allocated)
        add_calc = register.index("g_Chain.AddToCalcChain", published)
        initialized = register[allocated:published]
        for required in (
            "mgr->unused_40 = nullptr",
            "mgr->replayInputs = nullptr",
            "mgr->fpsCursor = nullptr",
            "mgr->stageReplayData = nullptr",
            "mgr->calcChain = nullptr",
            "mgr->drawChain = nullptr",
            "mgr->demoCalcChain = nullptr",
            "mgr->rngCalcChain = nullptr",
            "mgr->replayInputsByStage[i] = nullptr",
            "mgr->replayDataEndPointers[i] = 0u",
        ):
            with self.subTest(required=required):
                self.assertIn(required, initialized)
        self.assertLess(published, add_calc)

        first_add_failure = braced_block(
            register, "if (g_Chain.AddToCalcChain(mgr->calcChain, 16))"
        )
        self.assertIn("delete mgr->drawChain", first_add_failure)
        self.assertIn("mgr->drawChain = nullptr", first_add_failure)

    def test_stage_capacity_and_measured_high_water_are_exact(self) -> None:
        self.assertEqual(
            cpp_integer_array(self.enemy, "kUdLunaEnemyReservation"),
            [0, 72, 64, 64, 108, 64, 64],
        )
        self.assertEqual(
            cpp_integer_array(self.enemy, "kUdLunaEnemyHighWater"),
            [0, 69, 37, 24, 105, 17, 25],
        )
        prepare = function_body(
            self.enemy, "bool EnemyManager::PspPrepareEnemyManifest"
        )
        for required in (
            "kUdLunaEnemyReservation[stage]",
            "kUdLunaEnemyHighWater[stage]",
            "kEnemyBaseCapacity",
            "kEnemyChunkCapacity",
            "pspEnemyReservedCapacity",
            "arenaExtraBytes",
        ):
            with self.subTest(required=required):
                self.assertIn(required, prepare)

    def test_unmeasured_play_uses_the_existing_maximum_arena_envelope(self) -> None:
        unmeasured_capacity = cpp_integer(
            self.enemy, "kUnmeasuredEnemyReservation"
        )
        base_capacity = cpp_integer(self.enemy_h, "kEnemyBaseCapacity")
        maximum_extra = cpp_integer(self.arena, "kManifestMaxEnemyExtraBytes")

        self.assertEqual(unmeasured_capacity, 108)
        self.assertEqual(unmeasured_capacity % 4, 0)
        self.assertEqual(
            (unmeasured_capacity - base_capacity) * 20296, maximum_extra
        )
        self.assertEqual(maximum_extra, 893024)
        self.assertEqual(4640776 + maximum_extra, 5533800)
        self.assertEqual(
            unmeasured_capacity,
            max(cpp_integer_array(self.enemy, "kUdLunaEnemyReservation")),
        )

        prepare = function_body(
            self.enemy, "bool EnemyManager::PspPrepareEnemyManifest"
        )
        unmeasured_at = prepare.index(
            "reservation = kUnmeasuredEnemyReservation"
        )
        live_log = prepare.index("enemy manifest live bounded")
        live_branch = prepare[unmeasured_at : live_log + 180]
        self.assertIn("reservation = kUnmeasuredEnemyReservation", live_branch)
        self.assertNotIn("malloc", live_branch)
        self.assertNotIn("realloc", live_branch)

    def test_unmeasured_growth_preserves_udluna_demo_and_fail_loud_policy(self) -> None:
        prepare = function_body(
            self.enemy, "bool EnemyManager::PspPrepareEnemyManifest"
        )
        external_at = prepare.index(
            "const bool externalReplay = g_GameManager.replay && !g_GameManager.demo"
        )
        udluna = braced_block(
            prepare,
            "if (externalReplay && PspReplayMatchesUdLuna())",
            external_at,
        )
        demo_at = prepare.index("else if (g_GameManager.demo)", external_at)
        demo = braced_block(prepare, "else if (g_GameManager.demo)", external_at)
        live_log = prepare.index("enemy manifest live bounded", demo_at)

        self.assertLess(external_at, demo_at)
        self.assertLess(demo_at, live_log)
        self.assertIn("i32 reservation = kEnemyBaseCapacity", prepare)
        self.assertIn(
            "externalReplay && (!g_ReplayManager || !g_ReplayManager->data)",
            prepare,
        )
        self.assertRegex(prepare, r"REPLAY INVALID.*missing replay state")
        self.assertIn("kUdLunaEnemyReservation[stage]", udluna)
        self.assertNotIn("kUnmeasuredEnemyReservation", udluna)
        self.assertNotIn("kUnmeasuredEnemyReservation", demo)
        self.assertNotIn("reservation =", demo)
        self.assertIn("enemy manifest built-in demo S%d cap%d fail-loud", demo)

        fallback = prepare[demo_at + len(demo) :]
        self.assertIn("reservation = kUnmeasuredEnemyReservation", fallback)
        self.assertIn("if (externalReplay)", fallback)
        self.assertIn("enemy manifest replay bounded", fallback)
        self.assertIn("enemy manifest live bounded", fallback)
        self.assertNotRegex(fallback, r"REPLAY INVALID.*unknown replay")

        abort = function_body(
            self.enemy, "bool EnemyManager::PspAbortInvalidReplay"
        )
        self.assertIn(
            "g_GameManager.replay && !g_GameManager.demo ? 7 : 1", abort
        )

    def test_base_pool_budget_matches_all_pinned_payloads_and_alignment(self) -> None:
        pinned = (
            (self.bullet_h, "Bullet", 2276),
            (self.enemy, "Enemy", 20296),
            (self.item_h, "Item", 648),
            (self.effect_h, "Effect", 728),
        )
        for source, payload, size in pinned:
            with self.subTest(payload=payload):
                self.assertRegex(
                    source,
                    rf"static_assert\(sizeof\({payload}\)\s*==\s*{size}(?:u)?\s*,",
                )

        capacities = (
            cpp_integer(self.bullet_h, "kBulletCapacity"),
            cpp_integer(self.enemy_h, "kEnemyBaseCapacity"),
            cpp_integer(self.item_h, "kItemCapacity") + 1,
            cpp_integer(self.effect_h, "kNormalEffectCapacity")
            + cpp_integer(self.effect_h, "kSpecialEffectCapacity")
            + 1,
        )
        self.assertEqual(capacities, (1024, 64, 1101, 409))

        offset = 0
        for size, count in zip((2276, 20296, 648, 728), capacities):
            offset = (offset + 15) & ~15
            offset += size * count
        self.assertEqual(offset, 4640776)
        self.assertEqual(cpp_integer(self.arena, "kStageBasePoolBytes"), offset)

    def test_manifest_requires_the_audited_subset_instead_of_large_font_fallback(self) -> None:
        open_font = function_body(self.text, "static TTF_Font *OpenDefaultFont")
        feature = open_font.index(f"defined({MACRO})")
        fallback_loop = open_font.index(
            "for (const PspDefaultFontCandidate &candidate", feature
        )
        strict_path = open_font[feature:fallback_loop]
        self.assertIn("kPspDefaultFontCandidates[0]", strict_path)
        self.assertRegex(strict_path, r"(?is)REPLAY INVALID.*requires subset font")
        self.assertIn("return font", strict_path)

    def test_stage_start_grows_arena_once_after_anm_compaction_and_end_shrinks(self) -> None:
        gui_ready = self.game_added.index("Gui::RegisterChain")
        prepare = self.game_added.index("PspPrepareEnemyManifest", gui_ready)
        begin = self.game_added.index("th07_psp_1000_begin_pools", prepare)
        enemy_pool = self.game_added.index("PspEnsureEnemyPool", begin)
        self.assertLess(gui_ready, prepare)
        self.assertLess(prepare, begin)
        self.assertLess(begin, enemy_pool)

        arena_begin = function_body(self.arena, "bool th07_psp_1000_begin_pools")
        self.assertEqual(cpp_integer(self.arena, "kStageBasePoolBytes"), 4640776)
        self.assertEqual(
            cpp_integer(self.arena, "kManifestMaxEnemyExtraBytes"), 893024
        )
        self.assertIn(
            "kStageBasePoolBytes + kManifestMaxEnemyExtraBytes", self.arena
        )
        self.assertIn("kStageBasePoolBytes + stageExtraBytes", arena_begin)
        self.assertEqual(arena_begin.count("std::realloc("), 1)

        ensure = function_body(self.enemy, "bool EnemyManager::PspEnsureEnemyPool")
        self.assertIn("PspEnemySlotLimit()", ensure)
        self.assertIn("th07_psp_1000_alloc_pool", ensure)
        self.assertEqual(allocation_calls(ensure), [])

        arena_end = function_body(self.arena, "void th07_psp_1000_end_pools")
        self.assertIn("gArenaBytes > kPoolIdleArenaBytes", arena_end)
        self.assertIn("std::realloc(gArena, kPoolIdleArenaBytes)", arena_end)
        self.assertIn("th07_psp_1000_end_pools();", self.game_deleted)

        trim = function_body(self.arena, "void th07_psp_1000_trim_to_stage")
        self.assertIn("gArenaBytes <= kPoolIdleArenaBytes", trim)
        self.assertIn("std::realloc(gArena, kPoolIdleArenaBytes)", trim)

    def _assert_runtime_reserved_scan(self, signature: str) -> None:
        body = function_body(self.enemy, signature)
        self.assertIn("PspEnemySlotLimit()", body, signature)
        # Require a runtime-bound loop, while allowing the same source to keep
        # its ordinary kEnemyCapacity #else branch when the opt-in is disabled.
        # Looking through the loop's opening brace avoids mistaking the ')' in
        # PspEnemySlotLimit() for the end of the for header.
        loop_headers = re.findall(r"\bfor\s*\((.*?)\)\s*\{", body, re.DOTALL)

        assigned = re.findall(
            r"(?:const\s+)?i32\s+([A-Za-z_]\w*)\s*=\s*"
            r"(?:this->|arg->|g_EnemyManager\.)?PspEnemySlotLimit\(\)",
            body,
        )
        direct = any("PspEnemySlotLimit()" in loop for loop in loop_headers)
        bounded = direct or any(
            re.search(rf"<\s*{re.escape(name)}\b", loop)
            for name in assigned
            for loop in loop_headers
        )
        self.assertTrue(bounded, f"{signature} does not loop to its runtime limit")

    def test_all_six_enemy_scans_use_runtime_reserved_limit(self) -> None:
        for signature in (
            "Enemy *EnemyManager::SpawnEnemy(",
            "Enemy *EnemyManager::SpawnEnemyEx(",
            "i32 Enemy::HandleLifeCallback",
            "i32 Enemy::HandleTimerCallback",
            "u32 EnemyManager::OnUpdate",
            "i32 EnemyManager::RemoveAllEnemies",
        ):
            with self.subTest(signature=signature):
                self._assert_runtime_reserved_scan(signature)

    def test_spawn_never_allocates_and_exhaustion_uses_safe_fail_loud_hook(self) -> None:
        for signature in (
            "Enemy *EnemyManager::SpawnEnemy(",
            "Enemy *EnemyManager::SpawnEnemyEx(",
        ):
            with self.subTest(signature=signature):
                spawn = function_body(self.enemy, signature)
                self.assertEqual(allocation_calls(spawn), [])
                # A leading fail-fast is valid after a nested spawn has
                # latched exhaustion; a second/rfind call must handle the
                # ordinary scan reaching its reserved limit.
                exhausted = spawn.rindex("PspLatchEnemyManifestExhausted(eclSubId)")
                self.assertGreater(exhausted, spawn.index("for"))

        latch = function_body(
            self.enemy, "Enemy *EnemyManager::PspLatchEnemyManifestExhausted"
        )
        self.assertIn("pspEnemyManifestInvalid", latch)
        self.assertIn("return nullptr", latch)
        self.assertRegex(latch, r'(?is)REPLAY INVALID.*exhaust')
        timeline = function_body(
            self.enemy, "void EnemyManager::RunEclTimeline"
        )
        self.assertGreaterEqual(timeline.count("if (enemy)"), 2)
        switch_end_guard = timeline.index("if (g_EnemyManager.pspEnemyManifestInvalid)")
        pointer_advance = timeline.index("timeline->timelineInstr =", switch_end_guard)
        self.assertLess(switch_end_guard, pointer_advance)
        self.assertIn("return", timeline[switch_end_guard:pointer_advance])
        abort = function_body(self.enemy, "bool EnemyManager::PspAbortInvalidReplay")
        self.assertIn("pspEnemyAbortRequested", abort)
        self.assertIn("REPLAY INVALID", abort)

    def test_all_spawn_callers_remain_in_the_reviewed_null_return_inventory(self) -> None:
        self.assertEqual(self.enemy.count("g_EnemyManager.SpawnEnemy("), 8)
        self.assertEqual(
            self.enemy.count("g_EnemyManager.SpawnEnemyEx(")
            + self.ecl.count("g_EnemyManager.SpawnEnemyEx(")
            + self.enemy_ecl.count("g_EnemyManager.SpawnEnemyEx("),
            3,
        )

        timeline = function_body(
            self.enemy, "void EnemyManager::RunEclTimeline"
        )
        mirror_writes = [
            match.start()
            for match in re.finditer(r"enemy->mirror\s*=\s*1", timeline)
        ]
        self.assertEqual(len(mirror_writes), 2)
        for write in mirror_writes:
            guarded = timeline.rfind("if (enemy)", 0, write)
            self.assertNotEqual(guarded, -1)
            self.assertLess(write - guarded, 120)

    def test_unknown_replay_is_bounded_and_reservation_failure_is_invalid(self) -> None:
        prepare = function_body(
            self.enemy, "bool EnemyManager::PspPrepareEnemyManifest"
        )
        self.assertIn("PspReplayMatchesUdLuna()", prepare)
        self.assertRegex(prepare, r'(?is)replay bounded.*fail-loud')
        self.assertNotRegex(prepare, r'(?is)REPLAY INVALID.*unknown replay')
        self.assertIn(
            "g_GameManager.replay && !g_GameManager.demo", prepare
        )
        self.assertRegex(prepare, r'(?is)built-in demo.*cap.*fail-loud')

        pool_failure = braced_block(
            self.game_added, "if (!th07_psp_1000_begin_pools"
        )
        self.assertRegex(pool_failure, r'(?is)REPLAY INVALID.*reserv')
        self.assertIn("return ZUN_ERROR", pool_failure)

    def test_onupdate_aborts_after_timeline_and_nested_enemy_ecl_spawns(self) -> None:
        update = function_body(self.enemy, "u32 EnemyManager::OnUpdate")
        self.assertGreaterEqual(update.count("PspAbortInvalidReplay()"), 2)

        timeline = update.index("RunEclTimeline")
        remaining_timeline_guard = update.index(
            "if (arg->pspEnemyManifestInvalid)", timeline
        )
        timeline_abort = update.index("PspAbortInvalidReplay()", timeline)
        enemy_scan = update.index("arg->enemyCountReal", timeline_abort)
        self.assertLess(timeline, timeline_abort)
        self.assertLess(remaining_timeline_guard, timeline_abort)
        self.assertIn(
            "break", update[remaining_timeline_guard:timeline_abort]
        )
        self.assertLess(timeline_abort, enemy_scan)

        run_enemy_ecl = update.index("g_EclManager.RunEcl(enemy)", enemy_scan)
        nested_abort = update.index("PspAbortInvalidReplay()", run_enemy_ecl)
        self.assertLess(run_enemy_ecl, nested_abort)
        for abort in (timeline_abort, nested_abort):
            self.assertIn(
                "CHAIN_CALLBACK_RESULT_BREAK",
                update[abort : abort + 320],
                "invalid replay checkpoint must stop the current calc pass",
            )

    def test_supervisor_rolls_back_failed_external_replay_initialization(self) -> None:
        update = function_body(self.supervisor, "u32 Supervisor::OnUpdate")
        case_two = update.index("case 2:")
        failure = braced_block(
            update, "if (GameManager::RegisterChain() != ZUN_SUCCESS)", case_two
        )
        self.assertIn("g_GameManager.replay", failure)
        self.assertIn("GameManager::CutChain();", failure)
        self.assertIn("g_GameManager.SetReplay(0)", failure)
        self.assertIn("goto CASE_0", failure)

    def test_supervisor_discards_replay_after_final_stage_transition_failure(self) -> None:
        update = function_body(self.supervisor, "u32 Supervisor::OnUpdate")
        case_three = update.index("case 3:")
        first_failure = braced_block(
            update, "if (GameManager::RegisterChain() != ZUN_SUCCESS)", case_three
        )
        retry_marker = first_failure.index(
            "if (GameManager::RegisterChain() != ZUN_SUCCESS)"
        )
        final_failure = braced_block(
            first_failure,
            "if (GameManager::RegisterChain() != ZUN_SUCCESS)",
            retry_marker,
        )
        replay_gate = braced_block(final_failure, "if (g_GameManager.replay)")
        saved = replay_gate.index("ReplayManager::SaveReplay(NULL, NULL)")
        cleared = replay_gate.index("g_GameManager.SetReplay(0)")
        self.assertLess(saved, cleared)
        self.assertRegex(replay_gate, r"(?is)REPLAY INVALID.*discard")
        self.assertIn("goto CASE_0", final_failure)


if __name__ == "__main__":
    unittest.main()

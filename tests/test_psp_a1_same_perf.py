from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
GRAPHICS = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
    encoding="utf-8"
)
HEADER = (ROOT / "psp/graphics/PspGuGraphics.hpp").read_text(
    encoding="utf-8"
)
BULLETS = (ROOT / "src/BulletManager.cpp").read_text(encoding="utf-8")
ENEMIES = (ROOT / "src/EnemyManager.cpp").read_text(encoding="utf-8")
CALLER_PATHS = (
    "src/EclManager.cpp",
    "src/EnemyManager.cpp",
    "src/Gui.cpp",
    "src/ItemManager.cpp",
    "src/Player.cpp",
)
CALLERS = {path: (ROOT / path).read_text(encoding="utf-8") for path in CALLER_PATHS}


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


def target_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    match = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    return (
        makefile[start:]
        if match is None
        else makefile[start : start + len(target) + 1 + match.start()]
    )


def strip_exact_a1_blocks(source: str) -> str:
    return strip_exact_macro_blocks(source, "TH07_PSP_PERF_A1_SAME")


def strip_exact_macro_blocks(source: str, macro: str) -> str:
    directive = f"#if defined({macro})"
    result = source
    while directive in result:
        start = result.index(directive)
        depth = 0
        offset = start
        for line in result[start:].splitlines(keepends=True):
            stripped = line.lstrip()
            if re.match(r"#\s*(?:if|ifdef|ifndef)\b", stripped):
                depth += 1
            elif re.match(r"#\s*endif\b", stripped):
                depth -= 1
                if depth == 0:
                    result = result[:start] + result[offset + len(line) :]
                    break
            offset += len(line)
        else:
            raise AssertionError(f"unterminated {macro} preprocessor block")
    return result


class PspA1SamePerfTests(unittest.TestCase):
    def test_profile_is_default_off_guarded_and_stamped(self) -> None:
        self.assertIn("PSP_PERF_A1_SAME ?= 0", MAKEFILE)
        self.assertIn("-DTH07_PSP_PERF_A1_SAME", MAKEFILE)
        self.assertIn("PSP_PERF_A1_SAME requires PSP_PERF_DIAG=1", MAKEFILE)
        self.assertIn(
            "PSP_PERF_A1_SAME requires PSP_PERF_PROFILE=PERF_ACCEPT",
            MAKEFILE,
        )
        self.assertIn("PSP_PERF_A1_SAME is PSP-2000+-only", MAKEFILE)
        stamp = next(
            line for line in MAKEFILE.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_PERF_A1_SAME)", stamp)

    def test_candidate_is_exact_d2b_wrapper_plus_observer_identity(self) -> None:
        d2b = target_body(
            MAKEFILE, "psp3000-a6v4w-d2b-position-soa-read-build"
        )
        candidate = target_body(
            MAKEFILE, "psp3000-a6v4w-d2b-a1-same-observer-build"
        )
        setting_pattern = re.compile(r"(PSP_RID30_AB_ME_[A-Z0-9_]+)=([^ \\\n]+)")
        d2b_settings = dict(setting_pattern.findall(d2b))
        candidate_settings = dict(setting_pattern.findall(candidate))
        for key in (
            "PSP_RID30_AB_ME_UV16",
            "PSP_RID30_AB_ME_XYZ16",
            "PSP_RID30_AB_ME_C1_GE_EXPERIMENT",
            "PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY",
            "PSP_RID30_AB_ME_SEED_SOA",
            "PSP_RID30_AB_ME_POSITION_SOA_SHADOW",
            "PSP_RID30_AB_ME_POSITION_SOA_READ",
            "PSP_RID30_AB_ME_TITLE_WORKSPACE",
            "PSP_RID30_AB_ME_TITLE_TRANSIENT",
            "PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP",
            "PSP_RID30_AB_ME_LOCAL_FONT_SUBSET",
            "PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE",
        ):
            with self.subTest(setting=key):
                self.assertEqual(candidate_settings[key], d2b_settings[key])
        self.assertEqual(d2b_settings["PSP_RID30_AB_ME_PERF_A1_SAME"], "0")
        self.assertEqual(candidate_settings["PSP_RID30_AB_ME_PERF_A1_SAME"], "1")
        self.assertIn("PSP_RID30_AB_ME_BUILD_ID=0x260902a1u", candidate)
        self.assertIn("PSP_RID30_AB_ME_TITLE='TH07 D2B A1 SAME OBS'", candidate)
        self.assertEqual(MAKEFILE.count("0x260902a1u"), 1)

    def test_four_bulk_functions_have_only_entry_exit_timestamps(self) -> None:
        contracts = (
            (BULLETS, "void BulletManager::RemoveAllBullets", "REMOVE_ALL_BULLETS"),
            (BULLETS, "i32 BulletManager::DespawnBullets", "DESPAWN_BULLETS"),
            (BULLETS, "void BulletManager::RemoveBulletsInRadius", "REMOVE_RADIUS"),
            (ENEMIES, "i32 EnemyManager::RemoveAllEnemies", "REMOVE_ALL_ENEMIES"),
        )
        for source, signature, kind in contracts:
            with self.subTest(function=signature):
                body = function_body(source, signature)
                self.assertEqual(body.count("sceKernelGetSystemTimeWide()"), 2)
                self.assertEqual(body.count("Th07PspPerfTakeA1SameReason()"), 1)
                self.assertEqual(body.count("Th07PspPerfAddA1SameSample("), 1)
                self.assertIn(f"TH07_PSP_PERF_A1_{kind}", body)
                loop = body.index("for (")
                self.assertLess(body.index("perfA1StartUs"), loop)
                self.assertGreater(body.index("perfA1EndUs"), loop)

    def test_bulk_counters_follow_existing_side_effect_sites(self) -> None:
        remove_all = function_body(BULLETS, "void BulletManager::RemoveAllBullets")
        despawn = function_body(BULLETS, "i32 BulletManager::DespawnBullets")
        radius = function_body(BULLETS, "void BulletManager::RemoveBulletsInRadius")
        enemies = function_body(ENEMIES, "i32 EnemyManager::RemoveAllEnemies")

        self.assertEqual(
            remove_all.count("++perfA1Sample.itemAttempts;"),
            remove_all.count("g_ItemManager.SpawnItem("),
        )
        self.assertEqual(
            despawn.count("++perfA1Sample.itemAttempts;"),
            despawn.count("g_ItemManager.SpawnItem("),
        )
        self.assertEqual(radius.count("++perfA1Sample.itemAttempts;"), 1)
        self.assertEqual(radius.count("g_ItemManager.SpawnItem("), 1)
        self.assertIn("++perfA1Sample.popups;", despawn)
        self.assertIn("++perfA1Sample.auxiliary;", remove_all)
        self.assertIn("++perfA1Sample.auxiliary;", despawn)
        self.assertEqual(
            enemies.count("++perfA1Sample.itemAttempts;"),
            enemies.count("g_ItemManager.SpawnItem("),
        )
        self.assertEqual(
            enemies.count("++perfA1Sample.popups;"),
            enemies.count("g_AsciiManager.CreatePopup1("),
        )

    def test_bomb_observer_times_one_complete_loop_without_inner_clock_reads(self) -> None:
        update = function_body(BULLETS, "u32 BulletManager::OnUpdate")
        observer = update.index("const bool perfA1BombActive")
        loop = update.index("for (i = 0; i < kBulletCapacity; i++)", observer)
        publish = update.index("perfA1BombEndUs", loop)
        self.assertLess(observer, loop)
        self.assertLess(loop, publish)
        self.assertEqual(update[loop:publish].count("sceKernelGetSystemTimeWide()"), 0)
        self.assertIn("g_Player.pspBombClearHighWater > 0", update[observer:loop])
        self.assertIn("perfA1BombSample.eligible", update[publish:])
        self.assertEqual(update[loop:publish].count("++perfA1BombSample.affected;"), 2)
        self.assertEqual(update[loop:publish].count("++perfA1BombSample.itemAttempts;"), 2)
        self.assertEqual(
            update[loop:publish].count(
                "g_ItemManager.SpawnItem(&bullet->pos, g_Player.itemType, 1);"
            ),
            2,
        )
        self.assertIn("TH07_PSP_PERF_A1_REASON_BOMB", update[observer:loop])
        self.assertIn("TH07_PSP_PERF_A1_BOMB_BULLET_UPDATE", update[publish:])

    def test_every_authoritative_caller_has_an_explicit_reason(self) -> None:
        joined = "\n".join(CALLERS.values())
        call_pattern = re.compile(
            r"g_(?:BulletManager\.(?:RemoveAllBullets|DespawnBullets|"
            r"RemoveBulletsInRadius)|EnemyManager\.RemoveAllEnemies)\s*\("
        )
        calls = list(call_pattern.finditer(joined))
        self.assertEqual(len(calls), 16)
        for call in calls:
            with self.subTest(call=call.group(0)):
                prefix = joined[max(0, call.start() - 240) : call.start()]
                self.assertRegex(
                    prefix,
                    r"Th07PspPerfSetA1SameReason\([\s\S]*?\);\s*#endif\s*"
                    r"(?:[A-Za-z_]\w*\s*=\s*)?$",
                )

        expected_reason_counts = {
            "BEGIN_SPELL": 1,
            "SPELL_END": 2,
            "BOSS_DEFEAT": 2,
            "SPELL_TIMEOUT": 1,
            "ECL_ENEMY_CLEAR": 1,
            "ECL_BULLET_ITEM": 1,
            "ECL_RADIUS": 1,
            "ECL_BULLET_FADE": 1,
            "DIALOGUE": 2,
            "RESPAWN_GRACE": 1,
            "FULL_POWER": 3,
        }
        for reason, count in expected_reason_counts.items():
            with self.subTest(reason=reason):
                self.assertEqual(
                    joined.count(f"TH07_PSP_PERF_A1_REASON_{reason}"), count
                )

    def test_reason_marker_is_single_use_and_fail_closed(self) -> None:
        setter = function_body(GRAPHICS, "void Th07PspPerfSetA1SameReason")
        take = function_body(GRAPHICS, "unsigned int Th07PspPerfTakeA1SameReason")
        self.assertIn("gPerfA1SameNextReason != 0u", setter)
        self.assertIn("(reason & (reason - 1u)) != 0u", setter)
        self.assertIn("TH07_PSP_PERF_A1_REASON_UNKNOWN", setter)
        self.assertIn("gPerfA1SameNextReason = 0u", take)
        self.assertIn("TH07_PSP_PERF_A1_REASON_UNKNOWN", take)
        self.assertIn("++gPerfA1SameInvalid", take)

    def test_event_publish_has_no_io_allocation_or_clock_read(self) -> None:
        publish = function_body(GRAPHICS, "void Th07PspPerfAddA1SameSample")
        for forbidden in (
            "sceIo",
            "th07_psp_perf_note",
            "snprintf",
            "sceKernelGetSystemTimeWide",
            "malloc",
            "new ",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, publish)
        self.assertIn("PerfA1SameAdd(entry.elapsedUs, elapsedUs)", publish)
        self.assertIn("entry.reasons |=", publish)
        self.assertIn("entry.modes |=", publish)

    def test_sparse_line_is_after_accept_and_reset_at_window_boundary(self) -> None:
        report = function_body(GRAPHICS, "void ReportPerfWindow")
        accept_note = report.index("th07_psp_perf_note(acceptMessage)")
        a1_note = report.index("a1SameFormatValid ? a1SameMessage", accept_note)
        reset = report.index("ResetPerfWindowCounters();", a1_note)
        self.assertLess(accept_note, a1_note)
        self.assertLess(a1_note, reset)
        a1_region = report[accept_note:reset]
        self.assertIn("if (a1SameActivity)", a1_region)
        a5_off_region = strip_exact_macro_blocks(
            a1_region, "TH07_PSP_PERF_SFX_MIX"
        )
        self.assertEqual(a5_off_region.count("th07_psp_perf_note("), 2)
        self.assertEqual(a1_region.count("a1SameFormatValid ? a1SameMessage"), 1)
        reset_body = function_body(GRAPHICS, "void ResetPerfWindowCounters")
        self.assertIn("PerfA1SameReset();", reset_body)
        self.assertIn('"PERF A1S K%02X"', GRAPHICS)
        for label in ("RAB", "DSP", "RAD", "RAE", "BUP"):
            self.assertIn(f'length, "{label}"', GRAPHICS)

    def test_worst_case_a1_line_fits_formatter_and_ram_log_tag(self) -> None:
        u32 = 2**32 - 1
        u64 = 2**64 - 1
        tuples = [
            f"{label}{u32}/{u64}/{u32}/{u32}/{u32}/{u32}/{u32}/"
            f"{u32:08X}/{u32:08X}"
            for label in ("RAB", "DSP", "RAD", "RAE", "BUP")
        ]
        message = "PERF A1S K1F " + " ".join(tuples) + f" G{u32} O{u32}"
        tagged = f"PERF PFACCEPT RID{u32:08X} W{u32} " + message[5:]
        self.assertEqual(len(message), 576)
        self.assertLess(len(message) + 1, 600)
        self.assertLess(len(tagged) + 1, 640)
        self.assertIn("char a1SameMessage[600]", GRAPHICS)

    def test_sparse_format_omits_inactive_kind_tuples(self) -> None:
        formatter = function_body(GRAPHICS, "int PerfA1SameFormat")
        self.assertIn("activeMask |= 1u << kind", formatter)
        for kind in (
            "REMOVE_ALL_BULLETS",
            "DESPAWN_BULLETS",
            "REMOVE_RADIUS",
            "REMOVE_ALL_ENEMIES",
            "BOMB_BULLET_UPDATE",
        ):
            self.assertIn(f"activeMask & (1u << TH07_PSP_PERF_A1_{kind})", formatter)
        one_kind = (
            "PERF A1S K01 "
            "RAB1/100/10/10/10/0/0/00000002/00000002 G1 O0"
        )
        self.assertLess(len(one_kind), 96)

    def test_feature_off_removes_all_runtime_a1_references(self) -> None:
        for path, source in {
            **CALLERS,
            "src/BulletManager.cpp": BULLETS,
            "src/EnemyManager.cpp": ENEMIES,
        }.items():
            with self.subTest(path=path):
                stripped = strip_exact_a1_blocks(source)
                self.assertNotIn("Th07PspPerfSetA1SameReason", stripped)
                self.assertNotIn("Th07PspPerfTakeA1SameReason", stripped)
                self.assertNotIn("Th07PspPerfAddA1SameSample", stripped)
                self.assertNotIn("perfA1", stripped)

        stripped_graphics = strip_exact_a1_blocks(GRAPHICS)
        self.assertNotIn("PerfA1Same", stripped_graphics)
        self.assertNotIn("a1Same", stripped_graphics)
        stripped_header = strip_exact_a1_blocks(HEADER)
        self.assertNotIn("Th07PspPerfA1", stripped_header)


if __name__ == "__main__":
    unittest.main()

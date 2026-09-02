from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "analyze_a5_mixer", ROOT / "tools/analyze_a5_mixer.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def tag(window: int, *, profile: str = "A5M", run_id: str = "A5A50001") -> str:
    return f"PERF PF{profile} RID{run_id} W{window}"


def accept(
    window: int,
    *,
    stage: int = 6,
    frames: int = 120,
    p99_us: int = 17000,
    misses: int = 8,
    run_id: str = "A5A50001",
) -> str:
    elapsed_us = frames * 1_000_000 // 60
    hw_fps_x10 = frames * 10_000_000 // elapsed_us
    avg_us = 16000
    max_us = max(18000, p99_us)
    return (
        f"{tag(window, run_id=run_id)} ACCEPT S2 ST{stage} N{frames} "
        f"HWFPS{hw_fps_x10 // 10}.{hw_fps_x10 % 10} ELUS{elapsed_us} "
        f"AVG{avg_us // 1000}.{avg_us % 1000 // 100} "
        f"MAX{max_us // 1000}.{max_us % 1000 // 100} "
        f"P99{p99_us // 1000}.{p99_us % 1000 // 100} "
        f"OVR{misses} MISS{misses} AVGUS{avg_us} MAXUS{max_us} "
        f"P99US{p99_us} MEAVGUS0 MEFAULT0 "
        f"H{frames}/0/0/0/0/0/0/0/0/0 V1"
    )


def a5m(
    window: int,
    *,
    stage: int = 6,
    frames: int = 120,
    total_us: int = 60000,
    calls: int = 120,
    average_us: int | None = None,
    p99_us: int = 520,
    max_us: int = 550,
    voice_visits: int = 240,
    voice_max: int = 3,
    divisor_one_calls: int | None = None,
    triggers: int = 20,
    effect_current: int = 5,
    effect_max: int = 40,
    limited: int = 30,
    overflow: int = 0,
    integrity: int = 1,
    run_id: str = "A5A50001",
) -> str:
    if average_us is None:
        average_us = total_us // calls if calls else 0
    if divisor_one_calls is None:
        divisor_one_calls = calls
    return (
        f"{tag(window, run_id=run_id)} A5M S2 ST{stage} N{frames} "
        f"MU{total_us} MC{calls} MA{average_us} MP99{p99_us} MX{max_us} "
        f"AV{voice_visits} AVM{voice_max} D1{divisor_one_calls} "
        f"TR{triggers} FX{effect_current}/{effect_max} LIM{limited} "
        f"OF{overflow} G{integrity}"
    )


def zero_a5m(window: int, *, triggers: int = 0) -> str:
    return a5m(
        window,
        total_us=0,
        calls=0,
        average_us=0,
        p99_us=0,
        max_us=0,
        voice_visits=0,
        voice_max=0,
        triggers=triggers,
        limited=0,
    )


def end(
    window: int,
    *,
    valid: int = 1,
    drop: int = 0,
    run_id: str = "A5A50001",
) -> str:
    return f"{tag(window, run_id=run_id)} END VALID={valid} DROP={drop}"


class AnalyzeA5MixerTests(unittest.TestCase):
    def test_valid_run_computes_whole_cost_upper_bound_and_target_list(self) -> None:
        lines = [
            "[boot] unrelated line",
            accept(1, p99_us=17000, misses=8),
            a5m(1, total_us=60000),
            accept(2, p99_us=17400, misses=9),
            a5m(2, total_us=60000),
            end(2),
        ]
        result = MODULE.analyze(lines)

        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["profile"], "A5M")
        self.assertEqual(result["run_id"], "A5A50001")
        self.assertEqual(result["accept_windows"], 2)
        self.assertEqual(result["a5m_windows"], 2)
        self.assertFalse(result["performance_claim"])
        self.assertEqual([row["window"] for row in result["target_windows"]], [1])
        first = result["windows"][0]
        self.assertEqual(first["p99_deficit_us"], 333)
        self.assertEqual(
            first["mixer_whole_cost_upper_bound_us_per_frame"], 500.0
        )
        self.assertEqual(first["upper_bound_minus_deficit_us"], 167.0)

    def test_target_requires_a_miss_and_positive_deficit(self) -> None:
        lines = [
            accept(1, p99_us=17000, misses=0),
            a5m(1),
            accept(2, p99_us=16667, misses=8),
            a5m(2),
            end(2),
        ]
        result = MODULE.analyze(lines)
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["target_windows"], [])

    def test_zero_call_window_is_valid_and_trigger_proxy_is_independent(self) -> None:
        result = MODULE.analyze([accept(1), zero_a5m(1, triggers=3), end(1)])
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["windows"][0]["trigger_count"], 3)
        self.assertEqual(
            result["windows"][0]["mixer_whole_cost_upper_bound_us_per_frame"],
            0.0,
        )

    def test_requires_exact_a5m_grammar_without_extra_or_missing_fields(self) -> None:
        for malformed in (
            a5m(1) + " EXTRA1",
            a5m(1).replace(" MP99520", ""),
            a5m(1).replace("MU60000 MC120", "MC120 MU60000"),
            a5m(1).replace("FX5/40", "FX5.0/40"),
        ):
            with self.subTest(malformed=malformed):
                result = MODULE.analyze([accept(1), malformed, end(1)])
                self.assertFalse(result["valid"])
                self.assertTrue(
                    any("malformed PFA5M A5M" in error for error in result["errors"]),
                    result["errors"],
                )

    def test_requires_a5m_immediately_after_matching_accept(self) -> None:
        before = MODULE.analyze([a5m(1), accept(1), end(1)])
        self.assertFalse(before["valid"])
        self.assertTrue(
            any("immediately follow" in error for error in before["errors"])
        )

        separated = MODULE.analyze(
            [accept(1), f"{tag(1)} UNKNOWN X1", a5m(1), end(1)]
        )
        self.assertFalse(separated["valid"])
        self.assertTrue(
            any("immediately follow" in error for error in separated["errors"])
        )

        untagged_gap = MODULE.analyze(
            [accept(1), "unrelated interleaved line", a5m(1), end(1)]
        )
        self.assertFalse(untagged_gap["valid"])
        self.assertTrue(
            any("immediately follow" in error for error in untagged_gap["errors"])
        )

        missing = MODULE.analyze([accept(1), end(1)])
        self.assertFalse(missing["valid"])
        self.assertTrue(any("before A5M" in error for error in missing["errors"]))

    def test_rejects_duplicate_accept_or_a5m_window(self) -> None:
        duplicate_accept = MODULE.analyze(
            [accept(1), a5m(1), accept(1), a5m(1), end(1)]
        )
        self.assertFalse(duplicate_accept["valid"])
        self.assertTrue(
            any("duplicate ACCEPT" in error for error in duplicate_accept["errors"])
        )

        duplicate_a5m = MODULE.analyze([accept(1), a5m(1), a5m(1), end(1)])
        self.assertFalse(duplicate_a5m["valid"])
        self.assertTrue(
            any("duplicate A5M" in error for error in duplicate_a5m["errors"])
        )

    def test_rejects_mixed_identity_and_mismatched_window_state_stage_frames(self) -> None:
        mixed_rid = MODULE.analyze(
            [accept(1), a5m(1, run_id="A5A50002"), end(1)]
        )
        self.assertFalse(mixed_rid["valid"])
        self.assertTrue(
            any("exactly one PF/RID" in error for error in mixed_rid["errors"])
        )

        foreign_pf = MODULE.analyze(
            [accept(1), a5m(1), f"{tag(1, profile='ABME')} END VALID=1 DROP=0"]
        )
        self.assertFalse(foreign_pf["valid"])
        self.assertTrue(any("expected PFA5M" in error for error in foreign_pf["errors"]))

        for mismatch in (
            a5m(2),
            a5m(1, stage=5),
            a5m(1, frames=119),
            a5m(1).replace("A5M S2", "A5M S1"),
        ):
            with self.subTest(mismatch=mismatch):
                result = MODULE.analyze([accept(1), mismatch, end(1)])
                self.assertFalse(result["valid"])
                self.assertTrue(
                    any("W/S/ST/N" in error for error in result["errors"]),
                    result["errors"],
                )

    def test_requires_clean_final_end_but_allows_clean_stage_seals(self) -> None:
        for bad_end in (
            end(1, valid=0),
            end(1, drop=1),
            end(2),
        ):
            with self.subTest(bad_end=bad_end):
                result = MODULE.analyze([accept(1), a5m(1), bad_end])
                self.assertFalse(result["valid"])

        missing = MODULE.analyze([accept(1), a5m(1)])
        self.assertFalse(missing["valid"])
        self.assertTrue(any("missing PERF END" in error for error in missing["errors"]))
        self.assertTrue(
            any("final tagged PERF" in error for error in missing["errors"])
        )

        split = MODULE.analyze(
            [accept(1), a5m(1), end(1), accept(2), a5m(2), end(2)]
        )
        self.assertTrue(split["valid"], split["errors"])
        self.assertEqual(split["end_markers"], 2)

    def test_integrity_overflow_and_divisor_closure_are_fail_closed(self) -> None:
        mutations = (
            (a5m(1, integrity=0), "integrity G"),
            (a5m(1, overflow=1), "overflow OF"),
            (a5m(1, divisor_one_calls=119), "MC != D1"),
            (a5m(1, calls=513, divisor_one_calls=513), "timing capacity"),
        )
        for record, expected in mutations:
            with self.subTest(expected=expected):
                result = MODULE.analyze([accept(1), record, end(1)])
                self.assertFalse(result["valid"])
                self.assertTrue(
                    any(expected in error for error in result["errors"]),
                    result["errors"],
                )

    def test_rejects_inconsistent_timing_fields(self) -> None:
        mutations = (
            (a5m(1, average_us=499), "floor(MU/MC)"),
            (a5m(1, total_us=500, calls=2, max_us=600, p99_us=500,
                  voice_visits=2, voice_max=1), "MX exceeds MU"),
            (a5m(1, total_us=2000, calls=2, max_us=900, p99_us=800,
                  voice_visits=2, voice_max=1), "MU exceeds MC*MX"),
            (a5m(1, p99_us=551), "MP99 exceeds MX"),
            (a5m(1, total_us=0), "nonzero MC requires nonzero MU"),
            (zero_a5m(1).replace("MA0", "MA1"), "zero-call timing"),
        )
        for record, expected in mutations:
            with self.subTest(expected=expected):
                result = MODULE.analyze([accept(1), record, end(1)])
                self.assertFalse(result["valid"])
                self.assertTrue(
                    any(expected in error for error in result["errors"]),
                    result["errors"],
                )

    def test_rejects_impossible_voice_effect_and_limit_counts(self) -> None:
        mutations = (
            (a5m(1, voice_visits=119), "below one voice"),
            (a5m(1, voice_max=17), "AVM must be"),
            (a5m(1, voice_visits=361), "AV exceeds MC*AVM"),
            (a5m(1, effect_current=-1), "FX current"),
            (a5m(1, effect_current=41, effect_max=40), "FX current"),
            (a5m(1, effect_max=409), "EffectManager capacity"),
            (a5m(1, limited=120 * 1024 + 1), "LIM exceeds"),
            (zero_a5m(1).replace("LIM0", "LIM1"), "zero-call LIM"),
        )
        for record, expected in mutations:
            with self.subTest(expected=expected):
                result = MODULE.analyze([accept(1), record, end(1)])
                self.assertFalse(result["valid"])
                self.assertTrue(
                    any(expected in error for error in result["errors"]),
                    result["errors"],
                )

    def test_rejects_uint32_overflow_and_legacy_or_overflow_evidence(self) -> None:
        too_large = a5m(1).replace("TR20", f"TR{1 << 32}")
        result = MODULE.analyze([accept(1), too_large, end(1)])
        self.assertFalse(result["valid"])
        self.assertTrue(any("exceeds uint32" in error for error in result["errors"]))

        for forbidden, expected in (
            ("PERF OVERFLOW", "PERF log overflow"),
            ("FPS=59.9", "legacy FPS="),
            ("curFps 59", "curFps"),
        ):
            with self.subTest(forbidden=forbidden):
                bad = MODULE.analyze(
                    [forbidden, accept(1), a5m(1), end(1)]
                )
                self.assertFalse(bad["valid"])
                self.assertTrue(
                    any(expected in error for error in bad["errors"]),
                    bad["errors"],
                )


if __name__ == "__main__":
    unittest.main()

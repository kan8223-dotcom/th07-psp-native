from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "analyze_final60_perf", ROOT / "tools/analyze_final60_perf.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def tag(profile: str, run_id: str, window: int) -> str:
    return f"PERF PF{profile} RID{run_id} W{window}"


def histogram(profile: str, run_id: str, window: int, frames: int = 120) -> str:
    buckets = " ".join(f"H{index}{frames if index == 0 else 0}" for index in range(10))
    return f"{tag(profile, run_id, window)} HIST {buckets}"


def end(profile: str, run_id: str, window: int, valid: int = 1, drop: int = 0) -> str:
    return f"{tag(profile, run_id, window)} END VALID={valid} DROP={drop}"


def m2_window(run_id: str = "11111111", window: int = 1, frames: int = 120) -> list[str]:
    priorities = " ".join(f"P{priority:02d}X0.0" for priority in range(18))
    return [
        f"{tag('M2', run_id, window)} S2 ST4 N{frames}",
        histogram("M2", run_id, window, frames),
        f"{tag('M2', run_id, window)} OWNMAP I0 P10 A1234ABCD",
        f"{tag('M2', run_id, window)} DRAW {priorities} "
        "SUM0.0 R0.0 OH0.0 ERR0 LIM1 OOR0 OE0 OWNOV0 OWNTR0 G1",
        f"{tag('M2', run_id, window)} M2I I0 P10 TOTUS100 PKUS10 MXUS10 "
        f"STUS10 FLUS10 DCUS10 OTUS50 SUMUS100 ERR0 LIM{200 * frames} MM0 G1",
    ]


def m3_window(run_id: str = "22222222", window: int = 1, frames: int = 120) -> list[str]:
    return [
        f"{tag('M3', run_id, window)} S2 ST5 N{frames}",
        histogram("M3", run_id, window, frames),
        f"{tag('M3', run_id, window)} M3 BUUS100 LZUS10 ITUS20 BTUS70 "
        f"SUMUS100 ERR0 IERR0 LIM{200 * frames} F{frames}/{frames} "
        "NL0.0 NI0.0 NB0.2 VIS32 OOR0 G1",
        f"{tag('M3', run_id, window)} M3S RAWUS70 BTXUS70 LKUS10 VMUS10 VDUS10 "
        f"CRUS10 STUS10 VSUS10 RPUS5 DCUS5 SUMUS70 ERR0 LIM{200 * frames} "
        "CIUS0 CIDCUS0 COUS0 CODCUS0 CINB0 COUTB0 "
        "SAMP1/1/32 CULL0 BC1 DCN1 PEND0K EXUS0 "
        "TMRQ8256 FRAWUS146 POVUS96 REC1/0/0/0 MM0 MIX0 UNRES0 G1",
    ]


def accept_window(
    profile: str,
    run_id: str,
    window: int,
    stage: int,
    *,
    state: int = 2,
    frames: int = 120,
    avg_us: int = 15000,
    max_us: int = 16000,
    p99_us: int = 15500,
    over: int = 0,
    misses: int = 0,
    histogram_frames: int | None = None,
) -> str:
    histogram_frames = frames if histogram_frames is None else histogram_frames
    return (
        f"{tag(profile, run_id, window)} ACCEPT S{state} ST{stage} N{frames} "
        f"AVG{avg_us / 1000:.1f} MAX{max_us / 1000:.1f} P99{p99_us / 1000:.1f} "
        f"AVGUS{avg_us} MAXUS{max_us} P99US{p99_us} OVR{over} MISS{misses} "
        f"H{histogram_frames}/0/0/0/0/0/0/0/0/0 V1"
    )


def accept_run(
    profile: str = "ACCEPT",
    run_id: str = "33333333",
    stages: tuple[int, ...] = (4, 5, 6),
    avg_us: int = 15000,
    first_window: int = 1,
) -> list[str]:
    lines = [
        accept_window(profile, run_id, window, stage, avg_us=avg_us)
        for window, stage in enumerate(stages, first_window)
    ]
    lines.append(end(profile, run_id, first_window + len(stages) - 1))
    return lines


def a1_same(
    profile: str = "ACCEPT",
    run_id: str = "33333333",
    window: int = 1,
) -> str:
    return (
        f"{tag(profile, run_id, window)} A1S K01 "
        "RAB1/100/10/10/10/0/0/00000002/00000002 G1 O0"
    )


def split_stage_run(
    profile: str = "ACCEPT",
    run_id: str = "33333333",
    stages: tuple[int, ...] = (4, 5, 6),
    avg_us: int = 15000,
    first_window: int = 1,
) -> list[str]:
    lines: list[str] = []
    for window, stage in enumerate(stages, first_window):
        lines.append(
            accept_window(profile, run_id, window, stage, avg_us=avg_us)
        )
        lines.append(end(profile, run_id, window))
    return lines


class Final60PerfAnalyzerTests(unittest.TestCase):
    def test_accepts_optional_sparse_a1_same_line_after_accept(self) -> None:
        lines = accept_run()
        lines.insert(1, a1_same())
        result = MODULE.analyze(lines, "accept")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["a1_same_windows"], 1)
        self.assertEqual(len(result["a1_same_samples"]), 1)

    def test_rejects_a1_same_before_accept_or_more_than_once_per_window(self) -> None:
        before = accept_run()
        before.insert(0, a1_same())
        before_result = MODULE.analyze(before, "accept")
        self.assertFalse(before_result["valid"])
        self.assertTrue(
            any("A1S must follow" in error for error in before_result["errors"])
        )

        duplicate = accept_run()
        duplicate[1:1] = [a1_same(), a1_same()]
        duplicate_result = MODULE.analyze(duplicate, "accept")
        self.assertFalse(duplicate_result["valid"])
        self.assertTrue(
            any("surplus A1S" in error for error in duplicate_result["errors"])
        )

    def test_rejects_a1_same_integrity_reason_and_side_effect_faults(self) -> None:
        mutations = (
            (" G1 O0", " G0 O0", "observer integrity"),
            ("00000002/00000002", "00000001/00000002", "unexpected reason"),
            (
                "A1S K01",
                "A1S K05 RAD1/10/5/2/1/0/0/00000080/00000001",
                "RAD side-effect closure",
            ),
        )
        for old, new, expected_error in mutations:
            with self.subTest(expected_error=expected_error):
                lines = accept_run()
                lines.insert(1, a1_same().replace(old, new))
                result = MODULE.analyze(lines, "accept")
                self.assertFalse(result["valid"])
                self.assertTrue(
                    any(expected_error in error for error in result["errors"]),
                    result["errors"],
                )

    def test_accepts_complete_m2_run(self) -> None:
        lines = m2_window()
        lines.append(end("M2", "11111111", 1))
        result = MODULE.analyze(lines, "m2")
        self.assertTrue(result["valid"], result["errors"])

    def test_accepts_optional_apb_component_in_m2_window(self) -> None:
        lines = m2_window()
        # FB is telemetry, not a profiler-integrity latch. I3 acceptance may
        # separately require zero fallback without making this M2 window malformed.
        lines.append(f"{tag('M2', '11111111', 1)} APB CALL12 DIG345 FB1")
        lines.append(end("M2", "11111111", 1))
        result = MODULE.analyze(lines, "m2")
        self.assertTrue(result["valid"], result["errors"])

    def test_rejects_duplicate_or_malformed_apb_component(self) -> None:
        duplicate = m2_window()
        apb = f"{tag('M2', '11111111', 1)} APB CALL12 DIG345 FB0"
        duplicate.extend((apb, apb, end("M2", "11111111", 1)))
        duplicate_result = MODULE.analyze(duplicate, "m2")
        self.assertFalse(duplicate_result["valid"])
        self.assertTrue(any("surplus APB" in error for error in duplicate_result["errors"]))

        malformed = m2_window()
        malformed.append(f"{tag('M2', '11111111', 1)} APB CALL12 DIG345")
        malformed.append(end("M2", "11111111", 1))
        malformed_result = MODULE.analyze(malformed, "m2")
        self.assertFalse(malformed_result["valid"])
        self.assertTrue(any("APB missing FB" in error for error in malformed_result["errors"]))

        duplicate_field = m2_window()
        duplicate_field.append(
            f"{tag('M2', '11111111', 1)} APB CALL12 DIG345 FB0 CALL12"
        )
        duplicate_field.append(end("M2", "11111111", 1))
        duplicate_field_result = MODULE.analyze(duplicate_field, "m2")
        self.assertFalse(duplicate_field_result["valid"])
        self.assertTrue(
            any("APB duplicate CALL" in error for error in duplicate_field_result["errors"])
        )

    def test_rejects_impossible_apb_counts(self) -> None:
        for apb, expected_error in (
            ("CALL0 DIG0 FB0", "counters are all zero"),
            ("CALL0 DIG1 FB1", "DIG is nonzero without CALL"),
            ("CALL12 DIG11 FB0", "DIG is smaller"),
            ("CALL120 DIG345 FB1", "attempts exceed N120"),
        ):
            with self.subTest(apb=apb):
                lines = m2_window()
                lines.append(f"{tag('M2', '11111111', 1)} APB {apb}")
                lines.append(end("M2", "11111111", 1))
                result = MODULE.analyze(lines, "m2")
                self.assertFalse(result["valid"])
                self.assertTrue(
                    any(expected_error in error for error in result["errors"]),
                    result["errors"],
                )

    def test_rejects_m2_window_missing_required_component(self) -> None:
        lines = m2_window()
        lines = [line for line in lines if " HIST " not in line]
        lines.append(end("M2", "11111111", 1))
        result = MODULE.analyze(lines, "m2")
        self.assertFalse(result["valid"])
        self.assertTrue(any("hist line" in error for error in result["errors"]))

    def test_rejects_m2_without_owner_identity_map(self) -> None:
        lines = [line for line in m2_window() if " OWNMAP " not in line]
        lines.append(end("M2", "11111111", 1))
        result = MODULE.analyze(lines, "m2")
        self.assertFalse(result["valid"])
        self.assertTrue(any("no OWNMAP" in error for error in result["errors"]))

    def test_rejects_m2_missing_a_priority_even_with_g1(self) -> None:
        lines = m2_window()
        draw_index = next(index for index, line in enumerate(lines) if " DRAW " in line)
        lines[draw_index] = lines[draw_index].replace(" P17X0.0", "")
        lines.append(end("M2", "11111111", 1))
        result = MODULE.analyze(lines, "m2")
        self.assertFalse(result["valid"])
        self.assertTrue(any("P00..P17" in error for error in result["errors"]))

    def test_rejects_missing_or_arithmetically_false_m2i(self) -> None:
        missing = [line for line in m2_window() if " M2I " not in line]
        missing.append(end("M2", "11111111", 1))
        self.assertFalse(MODULE.analyze(missing, "m2")["valid"])

        false_sum = m2_window()
        m2i_index = next(index for index, line in enumerate(false_sum) if " M2I " in line)
        false_sum[m2i_index] = false_sum[m2i_index].replace(" SUMUS100", " SUMUS99")
        false_sum.append(end("M2", "11111111", 1))
        result = MODULE.analyze(false_sum, "m2")
        self.assertFalse(result["valid"])
        self.assertTrue(any("raw phase sum" in error for error in result["errors"]))

    def test_rejects_overflow_even_if_later_end_claims_valid(self) -> None:
        result = MODULE.analyze(
            [
                f"{tag('M2', '11111111', 1)} PROFILE INVALID OVERFLOW 1 LINES",
                end("M2", "11111111", 1),
            ],
            "auto",
        )
        self.assertFalse(result["valid"])

    def test_rejects_missing_end_and_failed_m3_closure(self) -> None:
        lines = m3_window()
        lines[2] = lines[2].replace(" G1", " G0")
        result = MODULE.analyze(lines, "m3")
        self.assertFalse(result["valid"])
        self.assertTrue(any("missing PERF END" in error for error in result["errors"]))
        # The partial run is intentionally not promoted to a complete run; G0 cannot mask it.

    def test_m3_requires_exactly_one_m3s_for_each_window(self) -> None:
        missing = m3_window()
        missing = [line for line in missing if " M3S " not in line]
        missing.append(end("M3", "22222222", 1))
        missing_result = MODULE.analyze(missing, "m3")
        self.assertFalse(missing_result["valid"])

        surplus = m3_window()
        surplus.append(surplus[-1])
        surplus.append(end("M3", "22222222", 1))
        surplus_result = MODULE.analyze(surplus, "m3")
        self.assertFalse(surplus_result["valid"])
        self.assertTrue(any("found 2" in error for error in surplus_result["errors"]))

    def test_m3s_from_another_window_cannot_satisfy_pair(self) -> None:
        lines = m3_window()
        lines[-1] = lines[-1].replace(" W1 ", " W2 ")
        lines.append(end("M3", "22222222", 2))
        result = MODULE.analyze(lines, "m3")
        self.assertFalse(result["valid"])
        self.assertTrue(any("W1" in error and "m3s" in error for error in result["errors"]))
        self.assertTrue(any("W2" in error and "m3" in error for error in result["errors"]))

    def test_m3_and_m3s_raw_closures_cannot_be_faked_by_g1(self) -> None:
        bad_m3 = m3_window()
        bad_m3[2] = bad_m3[2].replace(" SUMUS100", " SUMUS99")
        bad_m3.append(end("M3", "22222222", 1))
        m3_result = MODULE.analyze(bad_m3, "m3")
        self.assertFalse(m3_result["valid"])
        self.assertTrue(any("M3 raw phase sum" in error for error in m3_result["errors"]))

        bad_m3s = m3_window()
        bad_m3s[3] = bad_m3s[3].replace(" SUMUS70", " SUMUS69")
        bad_m3s.append(end("M3", "22222222", 1))
        m3s_result = MODULE.analyze(bad_m3s, "m3")
        self.assertFalse(m3s_result["valid"])
        self.assertTrue(any("M3S raw phase sum" in error for error in m3s_result["errors"]))

    def test_m3s_uses_the_runtime_closure_tolerance(self) -> None:
        within_gate = m3_window()
        within_gate[3] = (
            within_gate[3]
            .replace("LKUS10", "LKUS9")
            .replace("SUMUS70 ERR0", "SUMUS69 ERR1")
        )
        within_gate.append(end("M3", "22222222", 1))
        result = MODULE.analyze(within_gate, "m3")
        self.assertTrue(result["valid"], result["errors"])

    def test_raw_limits_cross_line_btus_and_sample_population_are_independent_gates(self) -> None:
        forged_draw = m2_window()
        forged_draw[3] = forged_draw[3].replace("ERR0 LIM1", "ERR300 LIM200")
        forged_draw.append(end("M2", "11111111", 1))
        draw_result = MODULE.analyze(forged_draw, "m2")
        self.assertFalse(draw_result["valid"])
        self.assertTrue(any("DRAW ERR exceeds LIM" in error for error in draw_result["errors"]))

        forged_m2i = m2_window()
        forged_m2i[4] = forged_m2i[4].replace("TOTUS100", "TOTUS30000").replace(
            "ERR0", "ERR29900"
        )
        forged_m2i.append(end("M2", "11111111", 1))
        m2i_result = MODULE.analyze(forged_m2i, "m2")
        self.assertFalse(m2i_result["valid"])
        self.assertTrue(any("ERR exceeds LIM" in error for error in m2i_result["errors"]))

        forged_limit = m3_window()
        forged_limit[2] = forged_limit[2].replace("LIM24000", "LIM999")
        forged_limit.append(end("M3", "22222222", 1))
        limit_result = MODULE.analyze(forged_limit, "m3")
        self.assertFalse(limit_result["valid"])
        self.assertTrue(any("LIM999 != derived" in error for error in limit_result["errors"]))

        crossed_btus = m3_window()
        crossed_btus[3] = (
            crossed_btus[3]
            .replace("RAWUS70", "RAWUS60")
            .replace("BTXUS70", "BTXUS60")
            .replace("LKUS10", "LKUS0")
            .replace("SUMUS70", "SUMUS60")
        )
        crossed_btus.append(end("M3", "22222222", 1))
        crossed_result = MODULE.analyze(crossed_btus, "m3")
        self.assertFalse(crossed_result["valid"])
        self.assertTrue(any("M3 BTUS70 != M3S RAWUS60" in error for error in crossed_result["errors"]))

        zero_samples = m3_window()
        zero_samples[3] = zero_samples[3].replace("SAMP1/1/32", "SAMP0/0/32")
        zero_samples.append(end("M3", "22222222", 1))
        sample_result = MODULE.analyze(zero_samples, "m3")
        self.assertFalse(sample_result["valid"])
        self.assertTrue(any("invalid SAMP population" in error for error in sample_result["errors"]))

        impossible_stride = m3_window()
        impossible_stride[3] = impossible_stride[3].replace(
            "SAMP1/1/32", "SAMP33/33/32"
        )
        impossible_stride.append(end("M3", "22222222", 1))
        stride_result = MODULE.analyze(impossible_stride, "m3")
        self.assertFalse(stride_result["valid"])
        self.assertTrue(
            any("invalid SAMP population" in error for error in stride_result["errors"])
        )

        impossible_culls = m3_window()
        impossible_culls[3] = impossible_culls[3].replace("CULL0", "CULL2")
        impossible_culls.append(end("M3", "22222222", 1))
        cull_result = MODULE.analyze(impossible_culls, "m3")
        self.assertFalse(cull_result["valid"])
        self.assertTrue(
            any("invalid SAMP population" in error for error in cull_result["errors"])
        )

        forged_visits = m3_window()
        forged_visits[2] = forged_visits[2].replace("NB0.2", "NB99.9")
        forged_visits.append(end("M3", "22222222", 1))
        visits_result = MODULE.analyze(forged_visits, "m3")
        self.assertFalse(visits_result["valid"])
        self.assertTrue(any("!= VIS32/N120" in error for error in visits_result["errors"]))

        dropped_emitters = m3_window()
        dropped_emitters[2] = dropped_emitters[2].replace("VIS32", "VIS64")
        dropped_emitters[2] = dropped_emitters[2].replace("NB0.2", "NB0.5")
        dropped_emitters.append(end("M3", "22222222", 1))
        dropped_result = MODULE.analyze(dropped_emitters, "m3")
        self.assertFalse(dropped_result["valid"])
        self.assertTrue(
            any("!= M3S emitter calls" in error for error in dropped_result["errors"])
        )

    def test_m3s_carry_adjustment_is_recomputed_not_trusted(self) -> None:
        valid_carry = m3_window()
        valid_carry[3] = (
            valid_carry[3]
            .replace("BTXUS70", "BTXUS60")
            .replace("LKUS10", "LKUS0")
            .replace("SUMUS70", "SUMUS60")
            .replace(" CIUS0", " CIUS10")
        )
        valid_carry.append(end("M3", "22222222", 1))
        valid_result = MODULE.analyze(valid_carry, "m3")
        self.assertTrue(valid_result["valid"], valid_result["errors"])

        forged_carry = list(valid_carry)
        forged_carry[3] = forged_carry[3].replace("BTXUS60", "BTXUS61")
        forged_result = MODULE.analyze(forged_carry, "m3")
        self.assertFalse(forged_result["valid"])
        self.assertTrue(any("carry-adjusted" in error for error in forged_result["errors"]))

        unresolved = m3_window()
        unresolved[3] = unresolved[3].replace("UNRES0", "UNRES1")
        unresolved.append(end("M3", "22222222", 1))
        unresolved_result = MODULE.analyze(unresolved, "m3")
        self.assertFalse(unresolved_result["valid"])
        self.assertTrue(any("unresolved carry" in error for error in unresolved_result["errors"]))

    def test_accept_summary_tracks_raw_worst_window(self) -> None:
        lines = [
            accept_window("ACCEPT", "33333333", 1, 4, max_us=16600, p99_us=15600),
            accept_window("ACCEPT", "33333333", 2, 5, max_us=16650, p99_us=15700),
            accept_window("ACCEPT", "33333333", 3, 6, max_us=16700, p99_us=15700),
            end("ACCEPT", "33333333", 3),
        ]
        result = MODULE.analyze(lines, "accept")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["accept_max_ms"], 16.7)
        self.assertEqual(result["accept_p99_ms"], 15.7)

    def test_histogram_sum_must_equal_n_for_accept_and_attrib(self) -> None:
        bad_accept = accept_run()
        bad_accept[0] = accept_window(
            "ACCEPT", "33333333", 1, 4, histogram_frames=119
        )
        accept_result = MODULE.analyze(bad_accept, "accept")
        self.assertFalse(accept_result["valid"])
        self.assertTrue(any("histogram sum" in error for error in accept_result["errors"]))

        bad_m2 = m2_window()
        bad_m2[1] = histogram("M2", "11111111", 1, 119)
        bad_m2.append(end("M2", "11111111", 1))
        m2_result = MODULE.analyze(bad_m2, "m2")
        self.assertFalse(m2_result["valid"])
        self.assertTrue(any("histogram sum" in error for error in m2_result["errors"]))

    def test_empty_timer_aa_uses_raw_us_and_profile_identity(self) -> None:
        baseline = MODULE.analyze(
            accept_run("ACCEPT", "33333333", (4, 5, 6), avg_us=15000),
            "accept",
            enforce_performance=False,
        )
        at_limit = MODULE.analyze(
            accept_run("EMPTY_M2", "44444444", (4, 5, 6), avg_us=15200),
            "accept",
            enforce_performance=False,
        )
        over_limit = MODULE.analyze(
            accept_run("EMPTY_M2", "55555555", (4, 5, 6), avg_us=15201),
            "accept",
            enforce_performance=False,
        )
        self.assertTrue(MODULE.compare_accept(baseline, at_limit)["valid"])
        self.assertFalse(MODULE.compare_accept(baseline, over_limit)["valid"])

    def test_aa_rejects_same_file_same_profile_and_same_rid(self) -> None:
        baseline = MODULE.analyze(
            accept_run("ACCEPT", "33333333"), "accept", enforce_performance=False
        )
        same_profile = MODULE.analyze(
            accept_run("ACCEPT", "44444444"), "accept", enforce_performance=False
        )
        same_rid = MODULE.analyze(
            accept_run("EMPTY_M3", "33333333"), "accept", enforce_performance=False
        )
        self.assertFalse(MODULE.compare_accept(baseline, same_profile)["valid"])
        self.assertFalse(MODULE.compare_accept(baseline, same_rid)["valid"])
        separate = MODULE.analyze(
            accept_run("EMPTY_M3", "55555555"), "accept", enforce_performance=False
        )
        comparison = MODULE.compare_accept(baseline, separate, same_source=True)
        self.assertFalse(comparison["valid"])
        self.assertTrue(any("same file" in error for error in comparison["errors"]))

    def test_perf_accept_rejects_raw_budget_failure_hidden_by_rounding(self) -> None:
        lines = accept_run()
        lines[1] = accept_window(
            "ACCEPT", "33333333", 2, 5, max_us=16701, p99_us=15701,
            over=1, misses=1,
        )
        result = MODULE.analyze(lines, "accept")
        self.assertFalse(result["valid"])
        self.assertTrue(any("MAXUS" in error for error in result["errors"]))

    def test_latest_end_delimited_stage1_run_cannot_be_masked_by_old_456(self) -> None:
        lines = accept_run("ACCEPT", "33333333", (4, 5, 6))
        lines.extend(accept_run("ACCEPT", "33333333", (1,), first_window=4))
        result = MODULE.analyze(lines, "accept")
        self.assertFalse(result["valid"])
        self.assertEqual(result["accept_stage_sequence"], [1])
        self.assertTrue(any("latest merged" in error for error in result["errors"]))

    def test_non_gameplay_accept_window_is_always_a_hard_failure(self) -> None:
        lines = accept_run("ACCEPT", "33333333", (4, 5, 6))
        lines.append(
            accept_window("ACCEPT", "33333333", 4, 1, state=1)
        )
        lines.append(end("ACCEPT", "33333333", 4))
        result = MODULE.analyze(lines, "accept")
        self.assertFalse(result["valid"])
        self.assertTrue(
            any("requires gameplay S2" in error for error in result["errors"])
        )

    def test_stage_end_segments_merge_into_one_hardware_playthrough(self) -> None:
        result = MODULE.analyze(split_stage_run(), "accept")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["end_markers"], 3)
        self.assertEqual(result["playthroughs"], 1)
        self.assertEqual(result["accept_stage_sequence"], [4, 5, 6])

    def test_new_rid_stage1_masks_an_older_split_456_playthrough(self) -> None:
        lines = split_stage_run("ACCEPT", "33333333")
        lines.extend(split_stage_run("ACCEPT", "44444444", (1,)))
        result = MODULE.analyze(lines, "accept")
        self.assertFalse(result["valid"])
        self.assertEqual(result["playthroughs"], 2)
        self.assertEqual(result["latest_run_id"], "44444444")
        self.assertEqual(result["accept_stage_sequence"], [1])

    def test_second_456_in_same_rid_is_a_new_latest_attempt(self) -> None:
        lines = split_stage_run("ACCEPT", "33333333", first_window=1)
        lines.extend(split_stage_run("ACCEPT", "33333333", first_window=4))
        result = MODULE.analyze(lines, "accept")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["playthroughs"], 2)
        self.assertEqual(result["accept_stage_sequence"], [4, 5, 6])
        self.assertEqual(result["latest_end_window"], 6)

    def test_extra_stage6_after_completed_run_cannot_reuse_old_456(self) -> None:
        lines = split_stage_run("ACCEPT", "33333333", first_window=1)
        lines.extend(
            split_stage_run("ACCEPT", "33333333", (6,), first_window=4)
        )
        result = MODULE.analyze(lines, "accept")
        self.assertFalse(result["valid"])
        self.assertEqual(result["playthroughs"], 2)
        self.assertEqual(result["accept_stage_sequence"], [6])

    def test_retried_same_stage_starts_new_attempt_and_cannot_form_false_456(self) -> None:
        lines = split_stage_run("ACCEPT", "33333333", (4, 5), first_window=1)
        lines.extend(
            split_stage_run("ACCEPT", "33333333", (5, 6), first_window=3)
        )
        result = MODULE.analyze(lines, "accept")
        self.assertFalse(result["valid"])
        self.assertEqual(result["playthroughs"], 2)
        self.assertEqual(result["accept_stage_sequence"], [5, 6])

    def test_aa_merges_split_stage_runs_and_requires_456_coverage(self) -> None:
        baseline = MODULE.analyze(
            split_stage_run("ACCEPT", "33333333"),
            "accept",
            enforce_performance=False,
        )
        probe = MODULE.analyze(
            split_stage_run("EMPTY_M2", "44444444"),
            "accept",
            enforce_performance=False,
        )
        self.assertTrue(MODULE.compare_accept(baseline, probe)["valid"])

        incomplete = MODULE.analyze(
            split_stage_run("EMPTY_M2", "55555555", (4, 5)),
            "accept",
            enforce_performance=False,
        )
        comparison = MODULE.compare_accept(baseline, incomplete)
        self.assertFalse(comparison["valid"])
        self.assertTrue(any("probe latest playthrough" in error for error in comparison["errors"]))

    def test_duplicate_shutdown_end_does_not_create_empty_latest_run(self) -> None:
        lines = accept_run()
        lines.append(end("ACCEPT", "33333333", 3))
        result = MODULE.analyze(lines, "accept")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["end_markers"], 2)

    def test_rejects_missing_metadata_and_cross_run_window_identity(self) -> None:
        missing = accept_run()
        missing[0] = missing[0].replace(" RID33333333", "")
        self.assertFalse(MODULE.analyze(missing, "accept")["valid"])

        crossed = m3_window()
        crossed[-1] = crossed[-1].replace("RID22222222", "RIDAAAAAAAA")
        crossed.append(end("M3", "22222222", 1))
        self.assertFalse(MODULE.analyze(crossed, "m3")["valid"])

    def test_rejects_duplicate_identity_critical_raw_and_end_fields(self) -> None:
        for identity_token in ("PFACCEPT", "RID33333333", "W1"):
            with self.subTest(identity_token=identity_token):
                duplicate_identity = accept_run()
                duplicate_identity[0] = duplicate_identity[0].replace(
                    f" {identity_token}", f" {identity_token} {identity_token}"
                )
                self.assertFalse(MODULE.analyze(duplicate_identity, "accept")["valid"])

        duplicate_accept = accept_run()
        duplicate_accept[0] = duplicate_accept[0].replace(
            " AVGUS15000", " AVGUS15000 AVGUS15000"
        )
        accept_result = MODULE.analyze(duplicate_accept, "accept")
        self.assertFalse(accept_result["valid"])
        self.assertTrue(any("duplicate AVGUS" in error for error in accept_result["errors"]))

        duplicate_summary = m2_window()
        duplicate_summary[0] += " N120"
        duplicate_summary.append(end("M2", "11111111", 1))
        summary_result = MODULE.analyze(duplicate_summary, "m2")
        self.assertFalse(summary_result["valid"])
        self.assertTrue(any("duplicate N" in error for error in summary_result["errors"]))

        duplicate_m2i = m2_window()
        duplicate_m2i[-1] += " TOTUS100"
        duplicate_m2i.append(end("M2", "11111111", 1))
        self.assertFalse(MODULE.analyze(duplicate_m2i, "m2")["valid"])

        duplicate_draw = m2_window()
        draw_index = next(index for index, line in enumerate(duplicate_draw) if " DRAW " in line)
        duplicate_draw[draw_index] += " G1"
        duplicate_draw.append(end("M2", "11111111", 1))
        self.assertFalse(MODULE.analyze(duplicate_draw, "m2")["valid"])

        duplicate_m3 = m3_window()
        duplicate_m3[2] += " BTUS70"
        duplicate_m3.append(end("M3", "22222222", 1))
        self.assertFalse(MODULE.analyze(duplicate_m3, "m3")["valid"])

        duplicate_m3s = m3_window()
        duplicate_m3s[3] += " SUMUS70"
        duplicate_m3s.append(end("M3", "22222222", 1))
        self.assertFalse(MODULE.analyze(duplicate_m3s, "m3")["valid"])

        duplicate_end = accept_run()
        duplicate_end[-1] += " VALID=1"
        end_result = MODULE.analyze(duplicate_end, "accept")
        self.assertFalse(end_result["valid"])
        self.assertTrue(any("duplicate VALID" in error for error in end_result["errors"]))

    def test_rejects_trailing_segment_after_an_older_end(self) -> None:
        lines = m2_window()
        lines.append(end("M2", "11111111", 1))
        lines.extend(m2_window(window=2))
        result = MODULE.analyze(lines, "m2")
        self.assertFalse(result["valid"])
        self.assertTrue(any("trailing partial" in error for error in result["errors"]))

    def test_allow_missing_end_is_explicit_debug_escape_hatch(self) -> None:
        result = MODULE.analyze(m3_window(), "m3", require_end=False)
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["end_markers"], 0)


if __name__ == "__main__":
    unittest.main()

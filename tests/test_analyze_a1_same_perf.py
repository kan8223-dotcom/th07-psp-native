from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "analyze_a1_same_perf", ROOT / "tools/analyze_a1_same_perf.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def base_accept(window: int = 1) -> str:
    return (
        f"PERF PFABME RID11111111 W{window} ACCEPT S2 ST4 N120 "
        "HWFPS60.0 ELUS2000000 AVG15.0 MAX16.0 P9915.5 OVR0 MISS0 "
        "AVGUS15000 MAXUS16000 P99US15500 MEAVGUS250 MEFAULT0 "
        "H120/0/0/0/0/0/0/0/0/0 V1"
    )


def d2b_accept(window: int = 1) -> str:
    extension = (
        "PSV10 PSM8 PSC2 PSWD3 PSWN5 PSWU1 "
        "PSP4 PSS2 PSI1 PSMV4 PSMM3 PSMC1 PSMD2 PSMK1 PSMF0 "
        "PSMR1/1/0/0 PSX0/0/0/0/0/0 PSB0/0 PSBM0/0 "
        "PSR1/120 PSVC8 PSG1 PSRA6 PSRH4 PSRF2 PSRX0/0/7 PSME5/0"
    )
    return base_accept(window).replace(" MEFAULT0 H", f" MEFAULT0 {extension} H")


def accept(window: int = 1) -> str:
    return d2b_accept(window)


def a1_same(window: int = 1, *, bomb: bool = False) -> str:
    active_mask = "10" if bomb else "01"
    active_tuple = (
        "BUP1/50/10/2/2/0/1/00001000/00000001"
        if bomb
        else "RAB1/100/10/10/10/0/0/00000002/00000002"
    )
    return (
        f"PERF PFABME RID11111111 W{window} A1S K{active_mask} "
        f"{active_tuple} G1 O0"
    )


def end(window: int = 1) -> str:
    return f"PERF PFABME RID11111111 W{window} END VALID=1 DROP=0"


class AnalyzeA1SamePerfTests(unittest.TestCase):
    def test_validates_and_aggregates_instrumented_abme_run(self) -> None:
        result = MODULE.analyze([accept(), a1_same(), end()])
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["a1_same_windows"], 1)
        self.assertEqual(result["totals"]["RAB"]["calls"], 1)
        self.assertEqual(result["observed_reasons"], ["BEGIN_SPELL"])
        self.assertTrue(result["reason_attribution_complete"])
        self.assertFalse(result["performance_claim"])

    def test_mixed_reason_window_keeps_kind_totals_but_is_not_reason_attributable(self) -> None:
        mixed_line = a1_same().replace(
            "RAB1/100/10/10/10/0/0/00000002/00000002",
            "RAB2/100/10/10/10/0/0/00000012/00000002",
        )
        result = MODULE.analyze([accept(), mixed_line, end()])
        self.assertTrue(result["valid"], result["errors"])
        self.assertFalse(result["reason_attribution_complete"])
        self.assertEqual(result["mixed_attribution"][0]["kind"], "RAB")

    def test_requires_an_event_and_strict_accept_end_evidence(self) -> None:
        missing = MODULE.analyze([accept(), end()])
        self.assertFalse(missing["valid"])
        self.assertTrue(any("missing sparse A1S" in error for error in missing["errors"]))

        invalid_end = MODULE.analyze(
            [accept(), a1_same(), end().replace("VALID=1", "VALID=0")]
        )
        self.assertFalse(invalid_end["valid"])
        self.assertTrue(any("END must be" in error for error in invalid_end["errors"]))

    def test_rejects_wrong_order_duplicate_window_and_faulty_tuple(self) -> None:
        wrong_order = MODULE.analyze([a1_same(), accept(), end()])
        self.assertFalse(wrong_order["valid"])
        self.assertTrue(any("immediately follow" in error for error in wrong_order["errors"]))

        duplicate = MODULE.analyze([accept(), a1_same(), a1_same(), end()])
        self.assertFalse(duplicate["valid"])
        self.assertTrue(any("duplicate A1S" in error for error in duplicate["errors"]))

        faulty = MODULE.analyze(
            [accept(), a1_same().replace(" G1 O0", " G0 O1"), end()]
        )
        self.assertFalse(faulty["valid"])
        self.assertTrue(any("integrity failed" in error for error in faulty["errors"]))
        self.assertTrue(any("counter overflow" in error for error in faulty["errors"]))

    def test_optional_reason_coverage_gate(self) -> None:
        missing = MODULE.analyze(
            [accept(), a1_same(), end()], required_reasons=("BOMB",)
        )
        self.assertFalse(missing["valid"])
        self.assertTrue(any("BOMB" in error for error in missing["errors"]))

        covered = MODULE.analyze(
            [accept(), a1_same(bomb=True), end()], required_reasons=("BOMB",)
        )
        self.assertTrue(covered["valid"], covered["errors"])

    def test_accepts_real_d2b_extension_without_weakening_base_parser(self) -> None:
        result = MODULE.analyze([d2b_accept(), a1_same(), end()])
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["accept_windows"], 1)
        self.assertEqual(result["hardware_timing"]["actual_fps"], 60.0)
        self.assertEqual(result["d2b"]["read_attempts"], 6)
        self.assertEqual(result["d2b"]["read_hits"], 4)
        self.assertEqual(result["d2b"]["read_fallbacks"], 2)
        self.assertEqual(result["d2b"]["read_hit_percent"], 66.666667)
        self.assertEqual(result["d2b"]["active_visits"], 10)
        self.assertEqual(result["d2b"]["matches"], 8)
        self.assertEqual(result["d2b"]["would_defer"], 3)
        self.assertEqual(result["d2b"]["unsupported_matches"], 5)
        self.assertEqual(result["d2b"]["eligible_percent"], 37.5)
        self.assertEqual(result["d2b"]["update_publishes"], 2)
        self.assertEqual(result["d2b"]["mutation_deferred"], 2)
        self.assertEqual(
            result["d2b"]["read_hits_per_eligible_percent"], 133.333333
        )

        missing_extension = MODULE.analyze(
            [base_accept(), a1_same(), end()]
        )
        self.assertFalse(missing_extension["valid"])
        self.assertTrue(
            any("missing D2B" in error for error in missing_extension["errors"])
        )

    def test_rejects_d2b_read_fault_and_broken_read_closure(self) -> None:
        faulty = d2b_accept().replace("PSRX0/0/7", "PSRX1/0/7")
        fault_result = MODULE.analyze([faulty, a1_same(), end()])
        self.assertFalse(fault_result["valid"])
        self.assertTrue(
            any("read faults" in error for error in fault_result["errors"])
        )

        broken = d2b_accept().replace("PSRF2", "PSRF3")
        closure_result = MODULE.analyze([broken, a1_same(), end()])
        self.assertFalse(closure_result["valid"])
        self.assertTrue(
            any("PSRH+PSRF != PSRA" in error for error in closure_result["errors"])
        )

    def test_rejects_mixed_base_and_d2b_accept_shapes(self) -> None:
        result = MODULE.analyze(
            [d2b_accept(1), a1_same(1), base_accept(2), end(2)]
        )
        self.assertFalse(result["valid"])
        self.assertTrue(
            any("extension coverage" in error for error in result["errors"])
        )

    def test_rejects_trailing_a1_tokens_and_non_adjacent_record(self) -> None:
        trailing = MODULE.analyze(
            [accept(), a1_same() + " EXTRA1", end()]
        )
        self.assertFalse(trailing["valid"])
        self.assertTrue(any("malformed A1S" in error for error in trailing["errors"]))

        non_adjacent = MODULE.analyze(
            [accept(), "unrelated log line", a1_same(), end()]
        )
        self.assertFalse(non_adjacent["valid"])
        self.assertTrue(
            any("immediately follow" in error for error in non_adjacent["errors"])
        )

    def test_requires_final_perf_record_to_be_end(self) -> None:
        result = MODULE.analyze(
            [accept(1), a1_same(1), end(1), accept(2), a1_same(2)]
        )
        self.assertFalse(result["valid"])
        self.assertTrue(
            any("final tagged PERF" in error for error in result["errors"])
        )

    def test_rejects_impossible_kind_semantics_and_bup_call_count(self) -> None:
        bad_rab = a1_same().replace(
            "RAB1/100/10/10/10/0/0", "RAB1/100/10/9/10/7/0"
        )
        rab_result = MODULE.analyze([accept(), bad_rab, end()])
        self.assertFalse(rab_result["valid"])
        self.assertTrue(
            any("RAB affected" in error for error in rab_result["errors"])
        )
        self.assertTrue(
            any("RAB popups" in error for error in rab_result["errors"])
        )

        bad_rab_mode = a1_same().replace(
            "00000002/00000002", "00000002/00000001"
        )
        rab_mode_result = MODULE.analyze(
            [accept(), bad_rab_mode, end()]
        )
        self.assertFalse(rab_mode_result["valid"])
        self.assertTrue(
            any("without item mode" in error for error in rab_mode_result["errors"])
        )

        bad_dsp = a1_same().replace(
            "K01 RAB1/100/10/10/10/0/0/00000002/00000002",
            "K02 DSP1/100/10/10/10/1/1/00000004/00000001",
        )
        dsp_result = MODULE.analyze([accept(), bad_dsp, end()])
        self.assertFalse(dsp_result["valid"])
        self.assertTrue(
            any("bullet/laser minimum" in error for error in dsp_result["errors"])
        )

        bad_rae = a1_same().replace(
            "K01 RAB1/100/10/10/10/0/0/00000002/00000002",
            "K08 RAE1/30/2/1/0/0/0/00000004/00000008",
        )
        rae_result = MODULE.analyze([accept(), bad_rae, end()])
        self.assertFalse(rae_result["valid"])
        self.assertTrue(
            any("no primary score mode" in error for error in rae_result["errors"])
        )

        bad_bup = a1_same(bomb=True).replace(
            "BUP1/50/10/2/2/0/1", "BUP999/50/1000/2/2/0/999"
        )
        bup_result = MODULE.analyze([accept(), bad_bup, end()])
        self.assertFalse(bup_result["valid"])
        self.assertTrue(
            any("BUP calls 999 exceed 2*N120+1" in error for error in bup_result["errors"])
        )

        impossible_reason = a1_same().replace(
            "00000002/00000002", "00000012/00000002"
        )
        reason_result = MODULE.analyze(
            [accept(), impossible_reason, end()]
        )
        self.assertFalse(reason_result["valid"])
        self.assertTrue(
            any("more reason bits than calls" in error for error in reason_result["errors"])
        )


if __name__ == "__main__":
    unittest.main()

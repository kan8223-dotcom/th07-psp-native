from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "compare_rid30_ab", ROOT / "tools/compare_rid30_ab.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


REPLAY_SHA = "A" * 64


def _x10(value_us: int) -> str:
    value = value_us // 100
    return f"{value // 10}.{value % 10}"


def accept(
    profile: str,
    run_id: str,
    window: int,
    *,
    stage: int = 6,
    frames: int = 120,
    elapsed_us: int = 2_000_000,
    avg_us: int = 5_000,
    max_us: int = 7_000,
    p99_us: int = 6_500,
    over_budget: int = 0,
    misses: int = 0,
    me_avg_us: int = 250,
    me_faults: int = 0,
    valid: int = 1,
    histogram_frames: int | None = None,
) -> str:
    hw_fps_x10 = frames * 10_000_000 // elapsed_us
    histogram_frames = frames if histogram_frames is None else histogram_frames
    return (
        f"[12.345] PERF PF{profile} RID{run_id} W{window} ACCEPT "
        f"S2 ST{stage} N{frames} HWFPS{hw_fps_x10 // 10}.{hw_fps_x10 % 10} "
        f"ELUS{elapsed_us} AVG{_x10(avg_us)} MAX{_x10(max_us)} "
        f"P99{_x10(p99_us)} OVR{over_budget} MISS{misses} "
        f"AVGUS{avg_us} MAXUS{max_us} P99US{p99_us} "
        f"MEAVGUS{me_avg_us} MEFAULT{me_faults} "
        f"H{histogram_frames}/0/0/0/0/0/0/0/0/0 V{valid}"
    )


def end(
    profile: str,
    run_id: str,
    window: int,
    *,
    valid: int = 1,
    drop: int = 0,
) -> str:
    return (
        f"[12.346] PERF PF{profile} RID{run_id} W{window} "
        f"END VALID={valid} DROP={drop}"
    )


def run_lines(
    profile: str,
    run_id: str,
    *,
    elapsed: tuple[int, ...] = (2_000_000,),
    frames: tuple[int, ...] | None = None,
    stages: tuple[int, ...] | None = None,
    me_avg_us: int | None = None,
) -> list[str]:
    frames = frames or tuple(120 for _ in elapsed)
    stages = stages or tuple(6 for _ in elapsed)
    if me_avg_us is None:
        me_avg_us = 250 if profile == "ABME" else 0
    lines = [
        accept(
            profile,
            run_id,
            index,
            frames=frame_count,
            stage=stage,
            elapsed_us=window_elapsed,
            me_avg_us=me_avg_us,
        )
        for index, (window_elapsed, frame_count, stage) in enumerate(
            zip(elapsed, frames, stages), 1
        )
    ]
    lines.append(end(profile, run_id, len(elapsed)))
    return lines


class Rid30AbComparisonTests(unittest.TestCase):
    def parse_me(self, lines: list[str]) -> object:
        return MODULE.parse_log(lines, "ABME")

    def parse_sc(self, lines: list[str]) -> object:
        return MODULE.parse_log(lines, "ABSC")

    def valid_pair(self) -> tuple[object, object]:
        return (
            self.parse_me(run_lines("ABME", "11111111")),
            self.parse_sc(run_lines("ABSC", "22222222")),
        )

    def test_aggregate_actual_fps_uses_sum_n_over_sum_elus(self) -> None:
        # Per-window HWFPS values are 60.0 and 50.0.  Their unweighted mean is
        # 55.0, while the required aggregate is 180 frames / 3.2 seconds =
        # 56.25 FPS.
        me = self.parse_me(
            run_lines(
                "ABME",
                "11111111",
                elapsed=(2_000_000, 1_200_000),
                frames=(120, 60),
            )
        )
        sc = self.parse_sc(
            run_lines(
                "ABSC",
                "22222222",
                elapsed=(2_400_000, 1_500_000),
                frames=(120, 60),
            )
        )
        result = MODULE.compare_runs(me, sc, REPLAY_SHA)
        self.assertTrue(result["valid"])
        self.assertEqual(result["fps_source"], "sum(N) * 1000000 / sum(ELUS)")
        self.assertEqual(result["me"]["summary"]["actual_fps"], 56.25)
        self.assertEqual(result["sc"]["summary"]["actual_fps"], 46.153846)

    def test_rejects_hwfps_inconsistent_with_n_and_elus(self) -> None:
        lines = run_lines("ABME", "11111111")
        lines[0] = lines[0].replace("HWFPS60.0", "HWFPS59.9")
        with self.assertRaisesRegex(MODULE.AuditError, "does not match N120/ELUS2000000"):
            self.parse_me(lines)

    def test_rejects_missing_elus_or_non_ab_record_shape(self) -> None:
        lines = run_lines("ABME", "11111111")
        lines[0] = lines[0].replace(" ELUS2000000", "")
        with self.assertRaisesRegex(MODULE.AuditError, "malformed or non-AB"):
            self.parse_me(lines)

    def test_rejects_legacy_shikigami_fps_even_beside_valid_ab_data(self) -> None:
        lines = run_lines("ABME", "11111111")
        lines.insert(0, "[TH07 STATUS] FPS=1 STATE=2 STAGE=6")
        with self.assertRaisesRegex(MODULE.AuditError, "forbidden legacy SHIKIGAMI FPS="):
            self.parse_me(lines)

    def test_rejects_curfps_even_beside_valid_ab_data(self) -> None:
        lines = run_lines("ABME", "11111111")
        lines.append("debug source=g_Supervisor.curFps value=60")
        with self.assertRaisesRegex(MODULE.AuditError, "forbidden curFps"):
            self.parse_me(lines)

    def test_rejects_mixed_legacy_perf_profile(self) -> None:
        lines = run_lines("ABME", "11111111")
        lines.insert(
            1,
            "PERF PFMERW0 RID33333333 W1 ACCEPT S2 ST6 N120 "
            "AVG5.0 MAX7.0 P996.5 OVR0 MISS0 AVGUS5000 MAXUS7000 "
            "P99US6500 H120/0/0/0/0/0/0/0/0/0 V1",
        )
        with self.assertRaisesRegex(MODULE.AuditError, "expected PFABME, got PFMERW0"):
            self.parse_me(lines)

    def test_rejects_wrong_ab_side_profile(self) -> None:
        with self.assertRaisesRegex(MODULE.AuditError, "expected PFABME, got PFABSC"):
            self.parse_me(run_lines("ABSC", "22222222"))

    def test_rejects_invalid_window_end_drop_and_overflow(self) -> None:
        mutations = {
            "window V0": lambda lines: lines.__setitem__(0, lines[0].replace(" V1", " V0")),
            "ME fault": lambda lines: lines.__setitem__(
                0, lines[0].replace(" MEFAULT0", " MEFAULT1")
            ),
            "END invalid": lambda lines: lines.__setitem__(1, end("ABME", "11111111", 1, valid=0)),
            "END drop": lambda lines: lines.__setitem__(1, end("ABME", "11111111", 1, drop=1)),
            "overflow": lambda lines: lines.insert(1, "PERF PROFILE INVALID OVERFLOW 3 LINES"),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label):
                lines = run_lines("ABME", "11111111")
                mutate(lines)
                with self.assertRaises(MODULE.AuditError):
                    self.parse_me(lines)

    def test_rejects_histogram_corruption_and_unknown_ab_records(self) -> None:
        corrupt_histogram = run_lines("ABME", "11111111")
        corrupt_histogram[0] = corrupt_histogram[0].replace(
            "H120/0/0/0/0/0/0/0/0/0", "H119/0/0/0/0/0/0/0/0/0"
        )
        with self.assertRaisesRegex(MODULE.AuditError, "histogram sum"):
            self.parse_me(corrupt_histogram)

        unknown = run_lines("ABME", "11111111")
        unknown.insert(1, "PERF PFABME RID11111111 W1 DENSE CUS1 G1")
        with self.assertRaisesRegex(MODULE.AuditError, "permits only ACCEPT and END"):
            self.parse_me(unknown)

    def test_rejects_multiple_rids_or_unsealed_run(self) -> None:
        multiple = run_lines("ABME", "11111111")
        multiple.insert(1, accept("ABME", "33333333", 2))
        with self.assertRaisesRegex(MODULE.AuditError, "exactly one RID"):
            self.parse_me(multiple)

        unsealed = run_lines("ABME", "11111111")[:-1]
        with self.assertRaisesRegex(MODULE.AuditError, "missing PERF END"):
            self.parse_me(unsealed)

    def test_allows_stage_end_markers_and_duplicate_final_shutdown_end(self) -> None:
        lines = [
            accept("ABME", "11111111", 1, stage=4),
            end("ABME", "11111111", 1),
            accept("ABME", "11111111", 2, stage=5),
            end("ABME", "11111111", 2),
            end("ABME", "11111111", 2),
        ]
        parsed = self.parse_me(lines)
        self.assertEqual(parsed.end_count, 3)
        self.assertEqual(len(parsed.windows), 2)

    def test_comparison_requires_exact_paired_window_identity(self) -> None:
        me = self.parse_me(run_lines("ABME", "11111111", stages=(6,)))
        sc = self.parse_sc(run_lines("ABSC", "22222222", stages=(5,)))
        with self.assertRaisesRegex(MODULE.AuditError, "identity differs"):
            MODULE.compare_runs(me, sc, REPLAY_SHA)

    def test_comparison_rejects_same_process_nonce(self) -> None:
        me = self.parse_me(run_lines("ABME", "11111111"))
        sc = self.parse_sc(run_lines("ABSC", "11111111"))
        with self.assertRaisesRegex(MODULE.AuditError, "same RID/process nonce"):
            MODULE.compare_runs(me, sc, REPLAY_SHA)

    def test_comparison_proves_me_activity_and_sc_inactivity(self) -> None:
        inactive_me = self.parse_me(run_lines("ABME", "11111111", me_avg_us=0))
        valid_sc = self.parse_sc(run_lines("ABSC", "22222222"))
        with self.assertRaisesRegex(MODULE.AuditError, "no window with measured ME work"):
            MODULE.compare_runs(inactive_me, valid_sc, REPLAY_SHA)

        valid_me = self.parse_me(run_lines("ABME", "11111111"))
        active_sc = self.parse_sc(run_lines("ABSC", "22222222", me_avg_us=1))
        with self.assertRaisesRegex(MODULE.AuditError, "PFABSC contains nonzero MEAVGUS"):
            MODULE.compare_runs(valid_me, active_sc, REPLAY_SHA)

    def test_replay_evidence_is_mandatory_and_validated(self) -> None:
        parser = MODULE.build_argument_parser()
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            parser.parse_args(["me.log", "sc.log"])

        me, sc = self.valid_pair()
        with self.assertRaisesRegex(MODULE.AuditError, "exactly 64 hexadecimal"):
            MODULE.compare_runs(me, sc, "not-a-sha")

    def test_replay_file_sha256_is_computed_from_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            replay = Path(tmpdir) / "same.rpy"
            replay.write_bytes(b"deterministic replay bytes")
            expected = hashlib.sha256(replay.read_bytes()).hexdigest().upper()
            self.assertEqual(MODULE.replay_sha256(replay), expected)

    def test_cli_json_happy_path_records_replay_hash(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            me_log = root / "me.log"
            sc_log = root / "sc.log"
            replay = root / "same.rpy"
            me_log.write_text(
                "\n".join(run_lines("ABME", "11111111")), encoding="utf-8"
            )
            sc_log.write_text(
                "\n".join(run_lines("ABSC", "22222222")), encoding="utf-8"
            )
            replay.write_bytes(b"the exact replay used for both runs")
            output = io.StringIO()
            with redirect_stdout(output), redirect_stderr(io.StringIO()):
                status = MODULE.main(
                    [
                        str(me_log),
                        str(sc_log),
                        "--replay",
                        str(replay),
                        "--json",
                    ]
                )
            self.assertEqual(status, 0)
            payload = json.loads(output.getvalue())
            self.assertTrue(payload["valid"])
            self.assertEqual(
                payload["replay_sha256"],
                hashlib.sha256(replay.read_bytes()).hexdigest().upper(),
            )


if __name__ == "__main__":
    unittest.main()

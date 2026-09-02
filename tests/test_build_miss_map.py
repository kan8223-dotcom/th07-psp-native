from __future__ import annotations

import importlib.util
import io
import csv
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_miss_map", ROOT / "tools/build_miss_map.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

BUILD_SHA = "A" * 64


def _x10(value_us: int) -> str:
    value = value_us // 100
    return f"{value // 10}.{value % 10}"


def accept(
    window: int,
    *,
    profile: str = "ABME",
    run_id: str = "11111111",
    stage: int = 6,
    frames: int = 120,
    avg_us: int = 12_000,
    max_us: int = 18_000,
    p99_us: int = 17_200,
    over_budget: int = 2,
    misses: int = 12,
    valid: int = 1,
    elapsed_us: int | None = 2_000_000,
    extension: str = "MEAVGUS250 MEFAULT0",
    histogram: tuple[int, ...] | None = None,
) -> str:
    histogram = histogram or (frames - over_budget, 0, 0, 0, 0, 0, over_budget, 0, 0, 0)
    hardware = ""
    if elapsed_us is not None:
        fps_x10 = frames * 10_000_000 // elapsed_us
        hardware = (
            f"HWFPS{fps_x10 // 10}.{fps_x10 % 10} ELUS{elapsed_us} "
        )
    extension_text = f" {extension}" if extension else ""
    return (
        f"[12.345] PERF PF{profile} RID{run_id} W{window} ACCEPT "
        f"S2 ST{stage} N{frames} {hardware}"
        f"AVG{_x10(avg_us)} MAX{_x10(max_us)} P99{_x10(p99_us)} "
        f"OVR{over_budget} MISS{misses} AVGUS{avg_us} MAXUS{max_us} "
        f"P99US{p99_us}{extension_text} "
        f"H{'/'.join(str(value) for value in histogram)} V{valid}"
    )


def end(
    window: int,
    *,
    profile: str = "ABME",
    run_id: str = "11111111",
    valid: int = 1,
    drop: int = 0,
) -> str:
    return (
        f"[12.346] PERF PF{profile} RID{run_id} W{window} "
        f"END VALID={valid} DROP={drop}"
    )


def write_log(directory: Path, name: str, lines: list[str]) -> Path:
    path = directory / name
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


class BuildMissMapTests(unittest.TestCase):
    def test_metric_contract_uses_p99_excess_and_keeps_avg_gap(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = write_log(
                Path(raw_dir),
                "valid.LOG",
                [
                    accept(1, avg_us=17_000, p99_us=17_200, misses=12),
                    end(1),
                ],
            )
            source = MODULE.parse_source(path)
        window = source.windows[0]
        self.assertEqual(window.average_gap_us, 333)
        self.assertEqual(window.average_shortfall_us, 333)
        self.assertEqual(window.p99_excess_us, 533)
        self.assertAlmostEqual(window.miss_density, 0.1)
        self.assertAlmostEqual(window.rank_score_us, 53.3)
        self.assertTrue(window.is_target(300, 800))

    def test_target_band_is_p99_proxy_not_average_gap(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = write_log(
                Path(raw_dir),
                "tail.LOG",
                [
                    accept(1, avg_us=17_400, p99_us=19_000, max_us=20_000),
                    end(1),
                ],
            )
            window = MODULE.parse_source(path).windows[0]
        self.assertEqual(window.average_shortfall_us, 733)
        self.assertEqual(window.p99_excess_us, 2_333)
        self.assertFalse(window.is_target(300, 800))

    def test_avg_headroom_can_still_be_a_p99_target(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = write_log(
                Path(raw_dir),
                "headroom.LOG",
                [accept(1, avg_us=12_000, p99_us=17_300), end(1)],
            )
            window = MODULE.parse_source(path).windows[0]
        self.assertEqual(window.average_gap_us, -4_667)
        self.assertEqual(window.p99_excess_us, 633)
        self.assertTrue(window.is_target(300, 800))

    def test_stage_local_identity_never_implies_cross_run_policy_exclusion(self) -> None:
        lines = [accept(index, stage=5) for index in range(1, 3)]
        lines.append(end(2))
        lines.extend(accept(index, stage=6) for index in range(3, 19))
        lines.append(end(18))
        with tempfile.TemporaryDirectory() as raw_dir:
            path = write_log(Path(raw_dir), "stages.LOG", lines)
            source = MODULE.parse_source(path)
            result = MODULE.build_map(
                MODULE.scan_sources([path]),
                path,
                reference_build_sha256=BUILD_SHA,
                policy_excluded_windows=(14,),
            )
        self.assertEqual(source.windows[2].stage_window, 1)
        stage_local_12 = source.windows[13]  # global W14, stage-6 local W12
        self.assertEqual(stage_local_12.stage_window, 12)
        self.assertTrue(stage_local_12.is_target(300, 800))
        self.assertFalse(
            stage_local_12.is_target(
                300, 800, policy_excluded_windows=(stage_local_12.window,)
            )
        )
        self.assertEqual(result["reference_policy_excluded"][0]["window"], 14)
        self.assertNotIn(14, [item["window"] for item in result["reference_targets"]])

    def test_ranking_is_independent_inside_one_source(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = write_log(
                Path(raw_dir),
                "rank.LOG",
                [
                    accept(1, p99_us=17_200, misses=12),
                    accept(2, p99_us=18_000, misses=24),
                    end(2),
                ],
            )
            source = MODULE.parse_source(path)
        ranked = MODULE.ranked_windows(source)
        self.assertEqual([window.window for window in ranked], [2, 1])

    def test_accepts_plain_and_extended_shapes(self) -> None:
        extension = (
            "MEAVGUS250 MEFAULT0 PSV1 PSM1 PSRA6 PSRH4 PSRF2 "
            "PSRX0/0/7 PSME5/0"
        )
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            extended = write_log(
                directory, "extended.LOG", [accept(1, extension=extension), end(1)]
            )
            plain = write_log(
                directory,
                "plain.LOG",
                [
                    accept(
                        1,
                        profile="ACCEPT",
                        run_id="22222222",
                        elapsed_us=None,
                        extension="",
                    ),
                    end(1, profile="ACCEPT", run_id="22222222"),
                ],
            )
            self.assertEqual(MODULE.parse_source(extended).profile, "ABME")
            self.assertEqual(MODULE.parse_source(plain).profile, "ACCEPT")

    def test_rejects_invalid_latch_overflow_unsealed_and_bad_end(self) -> None:
        cases = {
            "V0": [accept(1, valid=0), end(1)],
            "overflow": [accept(1), "PERF PROFILE INVALID OVERFLOW 1 LINES", end(1)],
            "unsealed": [accept(1)],
            "bad-end": [accept(1), end(1, drop=1)],
        }
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            for label, lines in cases.items():
                with self.subTest(label=label):
                    path = write_log(directory, f"{label}.LOG", lines)
                    with self.assertRaises(MODULE.AuditError):
                        MODULE.parse_source(path)

    def test_rejects_histogram_ovr_hwfps_mefault_and_duplicate_core(self) -> None:
        valid = accept(1)
        mutations = {
            "histogram": valid.replace(
                "H118/0/0/0/0/0/2/0/0/0", "H117/0/0/0/0/0/2/0/0/0"
            ),
            "ovr": valid.replace("OVR2", "OVR3"),
            "hwfps": valid.replace("HWFPS60.0", "HWFPS59.9"),
            "mefault": valid.replace("MEFAULT0", "MEFAULT1"),
            "duplicate": valid.replace(" P99US17200", " AVGUS12000 P99US17200"),
        }
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            for label, line in mutations.items():
                with self.subTest(label=label):
                    path = write_log(directory, f"{label}.LOG", [line, end(1)])
                    with self.assertRaises(MODULE.AuditError):
                        MODULE.parse_source(path)

    def test_rejects_fixed_30_and_noncontiguous_windows(self) -> None:
        cases = {
            "fixed30": ["fixed 30fps on", accept(1), end(1)],
            "gap": [accept(1), accept(3), end(3)],
        }
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            for label, lines in cases.items():
                with self.subTest(label=label):
                    path = write_log(directory, f"{label}.LOG", lines)
                    with self.assertRaises(MODULE.AuditError):
                        MODULE.parse_source(path)

    def test_scan_deduplicates_and_quarantines_whole_bad_sources(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            first = write_log(directory, "a.LOG", [accept(1), end(1)])
            nested = directory / "nested"
            nested.mkdir()
            alias = write_log(nested, "b.log", [accept(1), end(1)])
            write_log(directory, "bad.LOG", [accept(1, valid=0), end(1)])
            write_log(directory, "noise.LOG", ["boot only"])
            scan = MODULE.scan_sources([directory])
        self.assertEqual(scan.scanned_files, 4)
        self.assertEqual(scan.ignored_without_accept, 1)
        self.assertEqual(len(scan.sources), 1)
        self.assertEqual(len(scan.rejected), 1)
        self.assertEqual(len(scan.duplicate_aliases), 1)
        self.assertEqual(scan.duplicate_aliases[0], (alias, first))

    def test_window_range_parser_is_explicit_and_inclusive(self) -> None:
        self.assertEqual(MODULE._parse_window_spec("12-15"), (12, 13, 14, 15))
        self.assertEqual(MODULE._parse_window_spec("9"), (9,))
        with self.assertRaises(Exception):
            MODULE._parse_window_spec("15-12")

    def test_build_map_marks_only_fixed_reference_as_comparison_eligible(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            reference = write_log(directory, "reference.LOG", [accept(1), end(1)])
            observer = write_log(
                directory,
                "observer.LOG",
                [
                    accept(1, run_id="22222222"),
                    end(1, run_id="22222222"),
                ],
            )
            scan = MODULE.scan_sources([directory])
            result = MODULE.build_map(
                scan,
                reference,
                reference_build_sha256=BUILD_SHA,
                observer_on=(observer,),
            )
        self.assertEqual(result["reference"]["build_sha256"], BUILD_SHA)
        roles = {
            Path(item["source"]["path"]).name: item["source"]
            for item in result["source_maps"]
        }
        self.assertTrue(roles["reference.LOG"]["comparison_eligible"])
        self.assertFalse(roles["observer.LOG"]["comparison_eligible"])
        self.assertIn("observer-on", roles["observer.LOG"]["performance_role"])
        self.assertEqual(result["target_metric"], "P99 excess proxy; candidate only")

    def test_build_map_rejects_bad_build_sha_and_observer_reference_overlap(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            reference = write_log(directory, "reference.LOG", [accept(1), end(1)])
            scan = MODULE.scan_sources([directory])
            with self.assertRaisesRegex(MODULE.AuditError, "build SHA-256"):
                MODULE.build_map(
                    scan, reference, reference_build_sha256="BAD"
                )
            with self.assertRaisesRegex(MODULE.AuditError, "observer-on"):
                MODULE.build_map(
                    scan,
                    reference,
                    reference_build_sha256=BUILD_SHA,
                    observer_on=(reference,),
                )

    def test_csv_contains_every_window_with_source_local_ranks(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            reference = write_log(
                directory,
                "reference.LOG",
                [accept(1), accept(2, p99_us=18_000), end(2)],
            )
            other = write_log(
                directory,
                "other.LOG",
                [
                    accept(1, run_id="22222222"),
                    end(1, run_id="22222222"),
                ],
            )
            result = MODULE.build_map(
                MODULE.scan_sources([directory]),
                reference,
                reference_build_sha256=BUILD_SHA,
            )
            csv_text = MODULE.format_csv(result)
        rows = list(csv.DictReader(io.StringIO(csv_text)))
        self.assertEqual(len(rows), 3)
        self.assertEqual(sum(row["source_rank"] == "1" for row in rows), 2)
        self.assertIn("p99_excess_us", rows[0])
        self.assertIn("histogram", rows[0])

    def test_markdown_states_proxy_and_never_cross_ranks_sources(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            reference = write_log(directory, "reference.LOG", [accept(1), end(1)])
            result = MODULE.build_map(
                MODULE.scan_sources([directory]),
                reference,
                reference_build_sha256=BUILD_SHA,
            )
            report = MODULE.format_markdown(result)
        self.assertIn("P99 excess", report)
        self.assertIn("not a saving requirement", report)
        self.assertIn(BUILD_SHA, report)
        self.assertIn(result["reference"]["sha256"], report)
        self.assertIn("never cross-ranked", report)
        self.assertNotIn("Historical cross-source leads", report)

    def test_cli_requires_and_reports_fixed_reference_build_sha(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            directory = Path(raw_dir)
            reference = write_log(directory, "reference.LOG", [accept(1), end(1)])
            stdout = io.StringIO()
            stderr = io.StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                status = MODULE.main(
                    [
                        str(directory),
                        "--reference",
                        str(reference),
                        "--reference-build-sha256",
                        BUILD_SHA,
                        "--format",
                        "json",
                    ]
                )
        self.assertEqual(status, 0, stderr.getvalue())
        self.assertIn(BUILD_SHA, stdout.getvalue())

    def test_real_a7_reference_inventory_and_first_proxy_candidate(self) -> None:
        artifacts = ROOT / "artifacts"
        reference = artifacts / "TH07PSP_BOOT.pre-soa-review.20260901-232619.LOG"
        observer = artifacts / "TH07PSP_BOOT.A1-SAME-FPS-REGRESSION.20260902-012748.LOG"
        if not reference.exists() or not observer.exists():
            self.skipTest("hardware evidence artifacts are absent")
        result = MODULE.build_map(
            MODULE.scan_sources([artifacts]),
            reference,
            reference_build_sha256=(
                "9B58C04F5BECF1BA2438D7C74E65BD4B0B1D1AA7206308702F785D99741D83C5"
            ),
            observer_on=(observer,),
        )
        self.assertEqual(result["reference"]["sha256"], (
            "BA8711CA432144F9B91019B5F1F6C79D7164FDE06C751A6909FE82B5CDFF2F29"
        ))
        self.assertEqual(result["reference"]["windows"], 971)
        # A7 same-replay, Go@383 SC-only and later GO-ME hardware runs are
        # independent sources; none is cross-ranked into A7.  The hardware
        # evidence directory is append-only, so newer valid runs may increase
        # the inventory without changing the frozen A7 target map.
        self.assertGreaterEqual(result["inventory"]["scanned_files"], 164)
        self.assertGreaterEqual(result["inventory"]["validated_windows"], 9702)
        self.assertEqual(result["reference_targets"][0]["window"], 959)


if __name__ == "__main__":
    unittest.main()

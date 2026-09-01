from __future__ import annotations

import hashlib
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]

# Load the existing parser first under the canonical name used by the new
# comparator.  This keeps the test independent of the caller's PYTHONPATH.
HW_SPEC = importlib.util.spec_from_file_location(
    "compare_rid30_ab", ROOT / "tools" / "compare_rid30_ab.py"
)
assert HW_SPEC and HW_SPEC.loader
HW = importlib.util.module_from_spec(HW_SPEC)
sys.modules[HW_SPEC.name] = HW
HW_SPEC.loader.exec_module(HW)

SPEC = importlib.util.spec_from_file_location(
    "compare_d1_soa_ab", ROOT / "tools" / "compare_d1_soa_ab.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def _x10(value_us: int) -> str:
    value = value_us // 100
    return f"{value // 10}.{value % 10}"


def accept(
    run_id: str,
    window: int,
    *,
    stage: int = 6,
    frames: int = 120,
    elapsed_us: int = 2_000_000,
    avg_us: int = 10_000,
    max_us: int = 13_000,
    p99_us: int = 12_000,
    over_budget: int = 0,
    misses: int = 0,
    me_avg_us: int = 2_000,
    me_faults: int = 0,
    valid: int = 1,
) -> str:
    hwfps = frames * 10_000_000 // elapsed_us
    return (
        f"PERF PFABME RID{run_id} W{window} ACCEPT S2 ST{stage} N{frames} "
        f"HWFPS{hwfps // 10}.{hwfps % 10} ELUS{elapsed_us} "
        f"AVG{_x10(avg_us)} MAX{_x10(max_us)} P99{_x10(p99_us)} "
        f"OVR{over_budget} MISS{misses} AVGUS{avg_us} MAXUS{max_us} "
        f"P99US{p99_us} MEAVGUS{me_avg_us} MEFAULT{me_faults} "
        f"H{frames}/0/0/0/0/0/0/0/0/0 V{valid}"
    )


def run_lines(
    run_id: str,
    *,
    windows: int = 2,
    stage: int = 6,
    elapsed_us: int = 2_000_000,
    me_faults: int = 0,
) -> list[str]:
    lines = [
        accept(
            run_id,
            window,
            stage=stage,
            elapsed_us=elapsed_us,
            me_faults=me_faults,
        )
        for window in range(1, windows + 1)
    ]
    lines.append(
        f"PERF PFABME RID{run_id} W{windows} END VALID=1 DROP=0"
    )
    return lines


def parsed(run_id: str, **kwargs):
    return HW.parse_log(run_lines(run_id, **kwargs), "ABME")


def build(profile: str, sha_char: str):
    return MODULE.BuildEvidence(profile, Path(f"/{profile}.PBP"), sha_char * 64)


class D1SoaComparisonTests(unittest.TestCase):
    def test_profile_matrix_allows_only_hamming_distance_one_by_default(self) -> None:
        allowed = (
            ("A6V4W", "D1S0", "trusted_reader"),
            ("A6V4W", "D1A", "seed_layout"),
            ("D1S0", "D1B", "seed_layout"),
            ("D1A", "D1B", "trusted_reader"),
        )
        for index, (left, right, delta) in enumerate(allowed, 1):
            with self.subTest(left=left, right=right):
                result = MODULE.compare(
                    parsed(f"1111111{index}"),
                    parsed(f"2222222{index}"),
                    build(left, "A"),
                    build(right, "B"),
                    "C" * 64,
                    "C" * 64,
                    "L-ST6-FULL",
                )
                self.assertEqual(result["comparison_kind"], f"ONE DELTA: {delta}")
                self.assertTrue(result["causal_attribution_permitted"])

        with self.assertRaisesRegex(MODULE.AuditError, "exactly one profile delta"):
            MODULE.compare(
                parsed("11111111"),
                parsed("22222222"),
                build("A6V4W", "A"),
                build("D1B", "B"),
                "C" * 64,
                "C" * 64,
                "L-ST6-FULL",
            )

    def test_endpoint_is_explicit_and_never_claims_causal_attribution(self) -> None:
        result = MODULE.compare(
            parsed("11111111"),
            parsed("22222222", elapsed_us=2_100_000),
            build("A6V4W", "A"),
            build("D1B", "B"),
            "C" * 64,
            "C" * 64,
            "L-ST6-FULL",
            endpoint=True,
        )
        self.assertIn("COMBINED ENDPOINT", result["comparison_kind"])
        self.assertFalse(result["causal_attribution_permitted"])
        with self.assertRaisesRegex(MODULE.AuditError, "permits only"):
            MODULE.compare(
                parsed("33333333"),
                parsed("44444444"),
                build("A6V4W", "A"),
                build("D1A", "B"),
                "C" * 64,
                "C" * 64,
                "L-ST6-FULL",
                endpoint=True,
            )

    def test_uses_only_psp_n_over_elus_and_accept_timing(self) -> None:
        result = MODULE.compare(
            parsed("11111111", elapsed_us=2_000_000),
            parsed("22222222", elapsed_us=2_100_000),
            build("D1A", "A"),
            build("D1B", "B"),
            "C" * 64,
            "C" * 64,
            "L-ST6-FULL",
        )
        self.assertEqual(result["left"]["summary"]["actual_fps"], 60.0)
        self.assertEqual(
            result["right"]["summary"]["actual_fps"], 57.142857
        )
        self.assertIn("N/ELUS", result["performance_source"])
        self.assertEqual(
            result["forbidden_sources"],
            ["PC replay FPS", "curFps", "SHIKIGAMI FPS="],
        )
        self.assertEqual(result["left"]["summary"]["me_faults"], 0)
        self.assertEqual(result["windows"][0]["left"]["elapsed_us"], 2_000_000)
        self.assertEqual(result["windows"][0]["right"]["hwfps_x10"], 571)

    def test_rejects_replay_run_build_route_and_window_identity_errors(self) -> None:
        cases = (
            (
                "replay identities differ",
                dict(left_replay_sha256="C" * 64, right_replay_sha256="D" * 64),
            ),
            (
                "run identities are identical",
                dict(left_run=parsed("11111111"), right_run=parsed("11111111")),
            ),
            (
                "build identities are identical",
                dict(left_build=build("D1A", "A"), right_build=build("D1B", "A")),
            ),
            ("route identity", dict(route_id="")),
            (
                "window count differs",
                dict(
                    left_run=parsed("11111111", windows=2),
                    right_run=parsed("22222222", windows=1),
                ),
            ),
            (
                "route/window identity differs",
                dict(left_run=parsed("11111111", stage=6), right_run=parsed("22222222", stage=5)),
            ),
        )
        defaults = dict(
            left_run=parsed("11111111"),
            right_run=parsed("22222222"),
            left_build=build("D1A", "A"),
            right_build=build("D1B", "B"),
            left_replay_sha256="C" * 64,
            right_replay_sha256="C" * 64,
            route_id="L-ST6-FULL",
        )
        for expected, changes in cases:
            with self.subTest(expected=expected):
                values = {**defaults, **changes}
                with self.assertRaisesRegex(MODULE.AuditError, expected):
                    MODULE.compare(**values)

    def test_shared_parser_rejects_curfps_shikigami_fps_fault_drop_overflow(self) -> None:
        mutations = (
            ("forbidden curFps", lambda lines: lines.insert(0, "curFps=60")),
            ("forbidden legacy", lambda lines: lines.insert(0, "TH07 FPS=60")),
            (
                "MEFAULT1",
                lambda lines: lines.__setitem__(0, lines[0].replace("MEFAULT0", "MEFAULT1")),
            ),
            (
                "VALID=1 DROP=0",
                lambda lines: lines.__setitem__(-1, lines[-1].replace("DROP=0", "DROP=1")),
            ),
            ("overflow", lambda lines: lines.insert(1, "PERF LOG OVERFLOW")),
        )
        for expected, mutate in mutations:
            with self.subTest(expected=expected):
                lines = run_lines("11111111")
                mutate(lines)
                with self.assertRaisesRegex(MODULE.AuditError, expected):
                    HW.parse_log(lines, "ABME")

    def test_rejects_a_matching_window_hole_on_both_sides(self) -> None:
        left_lines = run_lines("11111111")
        right_lines = run_lines("22222222")
        left_lines[1] = left_lines[1].replace(" W2 ", " W3 ")
        left_lines[-1] = left_lines[-1].replace(" W2 ", " W3 ")
        right_lines[1] = right_lines[1].replace(" W2 ", " W3 ")
        right_lines[-1] = right_lines[-1].replace(" W2 ", " W3 ")
        left = HW.parse_log(left_lines, "ABME")
        right = HW.parse_log(right_lines, "ABME")
        with self.assertRaisesRegex(MODULE.AuditError, "missing/non-canonical"):
            MODULE.compare(
                left,
                right,
                build("D1A", "A"),
                build("D1B", "B"),
                "C" * 64,
                "C" * 64,
                "L-ST6-FULL",
            )

    def test_validate_build_checks_title_and_frozen_sha_for_every_profile(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            root = Path(tempdir)
            wrong = root / "wrong.pbp"
            wrong.write_bytes(b"TH07 A6V4W D1A SOA SHADOW")
            with self.assertRaisesRegex(MODULE.AuditError, "identity mismatch"):
                MODULE.validate_build("D1B", wrong)

            payloads = {
                "A6V4W": b"TH07 RID30 A6V4W CP932 WAVE",
                "D1S0": b"TH07 A6V4W D1S0 TRUSTED AOS",
                "D1A": b"TH07 A6V4W D1A SOA SHADOW",
                "D1B": b"TH07 A6V4W D1B SOA TRUSTED",
            }
            for profile, payload in payloads.items():
                with self.subTest(profile=profile):
                    candidate = root / f"{profile}.pbp"
                    candidate.write_bytes(payload)
                    digest = hashlib.sha256(payload).hexdigest().upper()
                    self.assertNotEqual(
                        digest, MODULE.EXPECTED_EBOOT_SHA256[profile]
                    )
                    with self.assertRaisesRegex(
                        MODULE.AuditError, "not the frozen matrix artifact"
                    ):
                        MODULE.validate_build(profile, candidate)
                    with patch.dict(
                        MODULE.EXPECTED_EBOOT_SHA256,
                        {profile: digest},
                    ):
                        evidence = MODULE.validate_build(profile, candidate)
                    self.assertEqual(evidence.sha256, digest)

    def test_compare_files_requires_distinct_logs_and_matching_replay_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            root = Path(tempdir)
            same_log = root / "same.log"
            same_log.write_text("\n".join(run_lines("11111111")), encoding="utf-8")
            replay = root / "replay.rpy"
            replay.write_bytes(b"same deterministic replay")
            left_eboot = root / "left.pbp"
            right_eboot = root / "right.pbp"
            left_eboot.write_bytes(b"TH07 A6V4W D1A SOA SHADOW")
            right_eboot.write_bytes(b"TH07 A6V4W D1B SOA TRUSTED")
            with self.assertRaisesRegex(MODULE.AuditError, "log identities are identical"):
                MODULE.compare_files(
                    same_log,
                    same_log,
                    "D1A",
                    "D1B",
                    left_eboot,
                    right_eboot,
                    replay,
                    replay,
                    "L-ST6-FULL",
                )

    def test_compare_files_happy_path_hashes_all_four_evidence_files(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            root = Path(tempdir)
            left_log = root / "left.log"
            right_log = root / "right.log"
            left_log.write_text(
                "\n".join(run_lines("11111111", elapsed_us=2_000_000)),
                encoding="utf-8",
            )
            right_log.write_text(
                "\n".join(run_lines("22222222", elapsed_us=2_100_000)),
                encoding="utf-8",
            )
            left_eboot = root / "left.pbp"
            right_eboot = root / "right.pbp"
            left_eboot.write_bytes(b"TH07 A6V4W D1A SOA SHADOW")
            right_eboot.write_bytes(b"TH07 A6V4W D1B SOA TRUSTED")
            left_replay = root / "left.rpy"
            right_replay = root / "right.rpy"
            left_replay.write_bytes(b"same deterministic replay")
            right_replay.write_bytes(left_replay.read_bytes())

            expected = {
                "D1A": hashlib.sha256(left_eboot.read_bytes()).hexdigest().upper(),
                "D1B": hashlib.sha256(right_eboot.read_bytes()).hexdigest().upper(),
            }
            with patch.dict(MODULE.EXPECTED_EBOOT_SHA256, expected):
                result = MODULE.compare_files(
                    left_log,
                    right_log,
                    "D1A",
                    "D1B",
                    left_eboot,
                    right_eboot,
                    left_replay,
                    right_replay,
                    "L-ST6-FULL",
                )
            self.assertTrue(result["valid"])
            self.assertEqual(
                result["left"]["build_sha256"],
                hashlib.sha256(left_eboot.read_bytes()).hexdigest().upper(),
            )
            self.assertEqual(
                result["right"]["log_sha256"],
                hashlib.sha256(right_log.read_bytes()).hexdigest().upper(),
            )


if __name__ == "__main__":
    unittest.main()

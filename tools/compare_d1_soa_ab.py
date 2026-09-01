#!/usr/bin/env python3
"""Fail-closed PSP hardware comparison for the D1 Bullet-SoA matrix.

This tool is deliberately separate from ``compare_rid30_ab.py``.  That tool
compares an ME build with an SC-only build; every profile here is an ME build.
The four profiles form a 2x2 matrix::

                  canonical reader       trusted reader
    BS11 AoS      A6V4W                  D1S0
    BS13 SoA      D1A                    D1B

Normal mode accepts only one edge of that matrix (one changed switch).  The
diagonal A6V4W/D1B comparison is available only with ``--endpoint`` and is
labelled as a combined endpoint for which causal attribution is forbidden.

Performance evidence comes exclusively from the PSP ``PFABME ... ACCEPT``
records.  Aggregate FPS is recomputed from N/ELUS.  The shared parser rejects
``curFps`` and legacy SHIKIGAMI ``FPS=`` text even when otherwise-valid PSP
records are present, so neither can accidentally become a performance source.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


def _load_hardware_parser():
    """Load the sibling parser both as a CLI and under importlib-based tests."""

    module_name = "compare_rid30_ab"
    if module_name in sys.modules:
        return sys.modules[module_name]
    path = Path(__file__).with_name("compare_rid30_ab.py")
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load PSP hardware parser: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


HW = _load_hardware_parser()
AuditError = HW.AuditError

SHA256_RE = HW.REPLAY_SHA256_RE
PROFILE_A6V4W = "A6V4W"
PROFILE_D1S0 = "D1S0"
PROFILE_D1A = "D1A"
PROFILE_D1B = "D1B"


@dataclass(frozen=True)
class ProfileSpec:
    name: str
    seed_layout: str
    trusted_reader: bool
    title_marker: bytes
    correctness_only: bool

    @property
    def switches(self) -> tuple[str, bool]:
        return (self.seed_layout, self.trusted_reader)


PROFILE_SPECS = {
    PROFILE_A6V4W: ProfileSpec(
        PROFILE_A6V4W,
        "BS11-AOS",
        False,
        b"TH07 RID30 A6V4W CP932 WAVE",
        False,
    ),
    PROFILE_D1S0: ProfileSpec(
        PROFILE_D1S0,
        "BS11-AOS",
        True,
        b"TH07 A6V4W D1S0 TRUSTED AOS",
        True,
    ),
    PROFILE_D1A: ProfileSpec(
        PROFILE_D1A,
        "BS13-SOA",
        False,
        b"TH07 A6V4W D1A SOA SHADOW",
        True,
    ),
    PROFILE_D1B: ProfileSpec(
        PROFILE_D1B,
        "BS13-SOA",
        True,
        b"TH07 A6V4W D1B SOA TRUSTED",
        False,
    ),
}

# All four isolated PC artifacts are immutable.  A file merely carrying the
# right PBP title is not enough to impersonate any member of the matrix.
EXPECTED_EBOOT_SHA256 = {
    PROFILE_A6V4W: (
        "DAF87978883A918597452B13510B7810D7CA9D931F7FFDF4E0BD02BCD3427B92"
    ),
    PROFILE_D1S0: (
        "A42A643C8D36DCC5F015E34136BFBDB1CC40E57955BDEA4A924EA72857C82898"
    ),
    PROFILE_D1A: (
        "29AD853CAA4FE15B67ADB29C090922F644C4FE28B39F54E63C0A7897EA8A950F"
    ),
    PROFILE_D1B: (
        "46969390B36A63BDEC5EBD7EF407020B27040A2A0845E8193CD2CFAA3F56B3C7"
    ),
}


@dataclass(frozen=True)
class BuildEvidence:
    profile: str
    path: Path
    sha256: str


def _sha256(path: Path, what: str) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise AuditError(f"cannot read {what} {path}: {exc}") from exc
    return digest.hexdigest().upper()


def _read_bytes(path: Path, what: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise AuditError(f"cannot read {what} {path}: {exc}") from exc


def validate_build(profile: str, path: Path) -> BuildEvidence:
    try:
        spec = PROFILE_SPECS[profile]
    except KeyError as exc:
        raise AuditError(f"unknown D1 matrix profile {profile!r}") from exc

    payload = _read_bytes(path, "EBOOT")
    if spec.title_marker not in payload:
        marker = spec.title_marker.decode("ascii")
        raise AuditError(
            f"{profile} EBOOT identity mismatch: missing PBP title {marker!r}"
        )
    digest = hashlib.sha256(payload).hexdigest().upper()
    expected_digest = EXPECTED_EBOOT_SHA256[profile]
    if digest != expected_digest:
        raise AuditError(
            f"{profile} EBOOT is not the frozen matrix artifact: "
            f"got {digest}, expected {expected_digest}"
        )
    return BuildEvidence(profile=profile, path=path, sha256=digest)


def _profile_delta(left: ProfileSpec, right: ProfileSpec) -> tuple[str, ...]:
    changed: list[str] = []
    if left.seed_layout != right.seed_layout:
        changed.append("seed_layout")
    if left.trusted_reader != right.trusted_reader:
        changed.append("trusted_reader")
    return tuple(changed)


def _aggregate(windows) -> dict[str, object]:
    frames = sum(window.frames for window in windows)
    elapsed_us = sum(window.elapsed_us for window in windows)
    weighted_avg_us = sum(window.avg_us * window.frames for window in windows)
    weighted_me_us = sum(window.me_avg_us * window.frames for window in windows)
    over_budget = sum(window.over_budget for window in windows)
    misses = sum(window.misses for window in windows)
    if frames <= 0 or elapsed_us <= 0:
        raise AuditError("aggregate has no positive PSP frame/time evidence")
    return {
        "windows": len(windows),
        "frames": frames,
        "elapsed_us": elapsed_us,
        "actual_fps": round(frames * 1_000_000 / elapsed_us, 6),
        "avg_us": round(weighted_avg_us / frames, 3),
        "worst_window_p99_us": max(window.p99_us for window in windows),
        "max_us": max(window.max_us for window in windows),
        "over_budget_frames": over_budget,
        "vsync_misses": misses,
        "me_avg_us": round(weighted_me_us / frames, 3),
        "me_faults": sum(window.me_faults for window in windows),
    }


def _window_evidence(left_windows, right_windows) -> list[dict[str, object]]:
    evidence: list[dict[str, object]] = []
    for left, right in zip(left_windows, right_windows):
        evidence.append(
            {
                "window": left.window,
                "state": left.state,
                "stage": left.stage,
                "frames": left.frames,
                "left": {
                    "hwfps_x10": left.hw_fps_x10,
                    "elapsed_us": left.elapsed_us,
                    "avg_us": left.avg_us,
                    "p99_us": left.p99_us,
                    "max_us": left.max_us,
                    "over_budget": left.over_budget,
                    "misses": left.misses,
                    "me_avg_us": left.me_avg_us,
                    "me_faults": left.me_faults,
                },
                "right": {
                    "hwfps_x10": right.hw_fps_x10,
                    "elapsed_us": right.elapsed_us,
                    "avg_us": right.avg_us,
                    "p99_us": right.p99_us,
                    "max_us": right.max_us,
                    "over_budget": right.over_budget,
                    "misses": right.misses,
                    "me_avg_us": right.me_avg_us,
                    "me_faults": right.me_faults,
                },
            }
        )
    return evidence


def compare(
    left_run,
    right_run,
    left_build: BuildEvidence,
    right_build: BuildEvidence,
    left_replay_sha256: str,
    right_replay_sha256: str,
    route_id: str,
    *,
    endpoint: bool = False,
) -> dict[str, object]:
    errors: list[str] = []
    try:
        left_spec = PROFILE_SPECS[left_build.profile]
        right_spec = PROFILE_SPECS[right_build.profile]
    except KeyError as exc:
        raise AuditError(f"unknown D1 matrix profile {exc.args[0]!r}") from exc

    if left_run.profile != HW.AB_ME_PROFILE or right_run.profile != HW.AB_ME_PROFILE:
        errors.append("both inputs must be PSP PFABME runs")
    if left_build.profile == right_build.profile:
        errors.append("profiles must differ")
    if left_build.sha256 == right_build.sha256:
        errors.append("build identities are identical")
    if left_run.run_id == right_run.run_id:
        errors.append("process run identities are identical")
    if SHA256_RE.fullmatch(left_replay_sha256) is None or SHA256_RE.fullmatch(
        right_replay_sha256
    ) is None:
        errors.append("each replay identity must be a 64-digit SHA-256")
    elif left_replay_sha256.upper() != right_replay_sha256.upper():
        errors.append("replay identities differ")
    if not route_id.strip():
        errors.append("a non-empty deterministic route identity is required")

    changed = _profile_delta(left_spec, right_spec)
    endpoint_pair = {left_spec.name, right_spec.name} == {
        PROFILE_A6V4W,
        PROFILE_D1B,
    }
    if endpoint:
        if not endpoint_pair:
            errors.append("--endpoint permits only the A6V4W/D1B diagonal")
    elif len(changed) != 1:
        errors.append(
            "default mode requires exactly one profile delta; "
            f"observed {list(changed)}"
        )

    if len(left_run.windows) != len(right_run.windows):
        errors.append(
            f"window count differs: left={len(left_run.windows)} "
            f"right={len(right_run.windows)}"
        )
    for side, run in (("left", left_run), ("right", right_run)):
        observed = [window.window for window in run.windows]
        expected = list(range(1, len(observed) + 1))
        if observed != expected:
            errors.append(
                f"{side} has missing/non-canonical windows: "
                f"observed={observed} expected={expected}"
            )
    for ordinal, (left_window, right_window) in enumerate(
        zip(left_run.windows, right_run.windows), 1
    ):
        if left_window.identity != right_window.identity:
            errors.append(
                f"paired window {ordinal} route/window identity differs: "
                f"left{left_window.identity} right{right_window.identity}"
            )

    if not any(window.me_avg_us > 0 for window in left_run.windows):
        errors.append(f"{left_spec.name} has no measured ME work")
    if not any(window.me_avg_us > 0 for window in right_run.windows):
        errors.append(f"{right_spec.name} has no measured ME work")

    if errors:
        raise AuditError("\n".join(errors))

    left_summary = _aggregate(left_run.windows)
    right_summary = _aggregate(right_run.windows)
    left_fps = float(left_summary["actual_fps"])
    right_fps = float(right_summary["actual_fps"])
    left_avg = float(left_summary["avg_us"])
    right_avg = float(right_summary["avg_us"])
    comparison_kind = "COMBINED ENDPOINT (causal attribution forbidden)" if endpoint else (
        f"ONE DELTA: {changed[0]}"
    )
    return {
        "valid": True,
        "comparison_kind": comparison_kind,
        "causal_attribution_permitted": not endpoint,
        "performance_source": "PSP PFABME N/ELUS and ACCEPT timing fields only",
        "forbidden_sources": ["PC replay FPS", "curFps", "SHIKIGAMI FPS="],
        "route_id": route_id.strip(),
        "replay_sha256": left_replay_sha256.upper(),
        "paired_identity": "W,S,ST,N",
        "windows": _window_evidence(left_run.windows, right_run.windows),
        "left": {
            "profile": left_spec.name,
            "seed_layout": left_spec.seed_layout,
            "trusted_reader": left_spec.trusted_reader,
            "correctness_only_profile": left_spec.correctness_only,
            "build_sha256": left_build.sha256,
            "run_id": left_run.run_id,
            "summary": left_summary,
        },
        "right": {
            "profile": right_spec.name,
            "seed_layout": right_spec.seed_layout,
            "trusted_reader": right_spec.trusted_reader,
            "correctness_only_profile": right_spec.correctness_only,
            "build_sha256": right_build.sha256,
            "run_id": right_run.run_id,
            "summary": right_summary,
        },
        "delta_right_minus_left": {
            "actual_fps": round(right_fps - left_fps, 6),
            "actual_fps_percent": round((right_fps / left_fps - 1.0) * 100.0, 6),
            "avg_us": round(right_avg - left_avg, 3),
            "worst_window_p99_us": int(right_summary["worst_window_p99_us"])
            - int(left_summary["worst_window_p99_us"]),
            "max_us": int(right_summary["max_us"])
            - int(left_summary["max_us"]),
            "over_budget_frames": int(right_summary["over_budget_frames"])
            - int(left_summary["over_budget_frames"]),
            "vsync_misses": int(right_summary["vsync_misses"])
            - int(left_summary["vsync_misses"]),
            "me_avg_us": round(
                float(right_summary["me_avg_us"])
                - float(left_summary["me_avg_us"]),
                3,
            ),
        },
    }


def compare_files(
    left_log: Path,
    right_log: Path,
    left_profile: str,
    right_profile: str,
    left_eboot: Path,
    right_eboot: Path,
    left_replay: Path,
    right_replay: Path,
    route_id: str,
    *,
    endpoint: bool = False,
) -> dict[str, object]:
    # Keep logs separate and make accidental same-file pairing explicit.
    left_log_sha = _sha256(left_log, "left log")
    right_log_sha = _sha256(right_log, "right log")
    if left_log_sha == right_log_sha:
        raise AuditError("log identities are identical")
    try:
        left_lines = left_log.read_text(encoding="utf-8", errors="replace").splitlines()
        right_lines = right_log.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise AuditError(f"cannot read hardware log: {exc}") from exc

    left_run = HW.parse_log(left_lines, HW.AB_ME_PROFILE)
    right_run = HW.parse_log(right_lines, HW.AB_ME_PROFILE)
    left_build = validate_build(left_profile, left_eboot)
    right_build = validate_build(right_profile, right_eboot)
    result = compare(
        left_run,
        right_run,
        left_build,
        right_build,
        _sha256(left_replay, "left replay"),
        _sha256(right_replay, "right replay"),
        route_id,
        endpoint=endpoint,
    )
    result["left"]["log_sha256"] = left_log_sha
    result["right"]["log_sha256"] = right_log_sha
    return result


def _format_text(result: dict[str, object]) -> str:
    left = result["left"]
    right = result["right"]
    delta = result["delta_right_minus_left"]
    left_summary = left["summary"]
    right_summary = right["summary"]
    assert isinstance(left, dict) and isinstance(right, dict)
    assert isinstance(delta, dict)
    assert isinstance(left_summary, dict) and isinstance(right_summary, dict)
    return "\n".join(
        (
            "D1 SOA PSP A/B VALID",
            str(result["comparison_kind"]),
            f"route: {result['route_id']}",
            f"replay SHA256: {result['replay_sha256']}",
            f"left:  {left['profile']} RID{left['run_id']} EBOOT {str(left['build_sha256'])[:12]}",
            f"right: {right['profile']} RID{right['run_id']} EBOOT "
            f"{str(right['build_sha256'])[:12]}",
            "metric                         left          right   right-left",
            f"actual FPS (sum N/sum ELUS)  {float(left_summary['actual_fps']):10.3f} "
            f"{float(right_summary['actual_fps']):10.3f} "
            f"{float(delta['actual_fps']):+12.3f}",
            f"AVGUS                         {float(left_summary['avg_us']):10.1f} "
            f"{float(right_summary['avg_us']):10.1f} "
            f"{float(delta['avg_us']):+12.1f}",
            f"worst P99US                   {int(left_summary['worst_window_p99_us']):10d} "
            f"{int(right_summary['worst_window_p99_us']):10d} "
            f"{int(delta['worst_window_p99_us']):+12d}",
            f"MAXUS                         {int(left_summary['max_us']):10d} "
            f"{int(right_summary['max_us']):10d} "
            f"{int(delta['max_us']):+12d}",
            f"OVR frames                    {int(left_summary['over_budget_frames']):10d} "
            f"{int(right_summary['over_budget_frames']):10d} "
            f"{int(delta['over_budget_frames']):+12d}",
            f"MISS                          {int(left_summary['vsync_misses']):10d} "
            f"{int(right_summary['vsync_misses']):10d} "
            f"{int(delta['vsync_misses']):+12d}",
            f"MEAVGUS                       {float(left_summary['me_avg_us']):10.1f} "
            f"{float(right_summary['me_avg_us']):10.1f} "
            f"{float(delta['me_avg_us']):+12.1f}",
        )
    )


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Compare two PSP PFABME runs from the A6V4W/D1 SoA 2x2 matrix. "
            "Default mode accepts only a one-switch delta."
        )
    )
    choices = tuple(PROFILE_SPECS)
    parser.add_argument("--left-profile", required=True, choices=choices)
    parser.add_argument("--left-log", required=True, type=Path)
    parser.add_argument("--left-eboot", required=True, type=Path)
    parser.add_argument("--left-replay", required=True, type=Path)
    parser.add_argument("--right-profile", required=True, choices=choices)
    parser.add_argument("--right-log", required=True, type=Path)
    parser.add_argument("--right-eboot", required=True, type=Path)
    parser.add_argument("--right-replay", required=True, type=Path)
    parser.add_argument(
        "--route-id",
        required=True,
        help="deterministic route label shared by both runs, e.g. L-ST6-FULL",
    )
    parser.add_argument(
        "--endpoint",
        action="store_true",
        help=(
            "allow only the A6V4W/D1B diagonal and label it as a combined, "
            "non-causal endpoint"
        ),
    )
    parser.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)
    try:
        result = compare_files(
            args.left_log,
            args.right_log,
            args.left_profile,
            args.right_profile,
            args.left_eboot,
            args.right_eboot,
            args.left_replay,
            args.right_replay,
            args.route_id,
            endpoint=args.endpoint,
        )
    except AuditError as exc:
        print(f"D1 SOA PSP A/B INVALID: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(_format_text(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

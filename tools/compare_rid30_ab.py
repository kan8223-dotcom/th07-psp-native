#!/usr/bin/env python3
"""Fail-closed comparison of RID30 real-hardware ABME and ABSC logs.

Only the dedicated ``PFABME``/``PFABSC`` ``PERF ACCEPT`` record is accepted.
Its ``HWFPS`` value is checked against the same record's PSP system-clock
``ELUS`` value, but aggregate FPS is always recomputed as::

    sum(N) * 1_000_000 / sum(ELUS)

Replay-era ``FPS=`` telemetry and ``curFps`` are not timing evidence.  Their
presence makes the input invalid so they can never be selected accidentally.
Likewise, legacy PERF profiles are rejected instead of being silently mixed
with the dedicated A/B profile.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


AB_ME_PROFILE = "ABME"
AB_SC_PROFILE = "ABSC"
AB_PROFILES = frozenset((AB_ME_PROFILE, AB_SC_PROFILE))
REPLAY_SHA256_RE = re.compile(r"[0-9a-fA-F]{64}\Z")

TAGGED_PERF_RE = re.compile(
    r"(?:^|\s)PERF\s+PF([A-Z0-9_]+)\s+RID([0-9A-Fa-f]{8})\s+W(\d+)\s+(.+?)\s*$"
)
LEGACY_FPS_RE = re.compile(r"(?<![A-Za-z0-9_])FPS\s*=", re.IGNORECASE)
CUR_FPS_RE = re.compile(r"curFps", re.IGNORECASE)
PERF_OVERFLOW_RE = re.compile(r"\bPERF\b.*\bOVERFLOW\b", re.IGNORECASE)

# Keep this exact.  The dedicated build intentionally emits one compact,
# fixed-shape record per timing window.  Accepting optional/unknown fields here
# would make it too easy to feed a legacy profile to the A/B tool.
ACCEPT_RE = re.compile(
    r"ACCEPT "
    r"S(?P<state>-?\d+) ST(?P<stage>-?\d+) N(?P<frames>\d+) "
    r"HWFPS(?P<hw_fps_whole>\d+)\.(?P<hw_fps_tenth>\d) "
    r"ELUS(?P<elapsed_us>\d+) "
    r"AVG(?P<avg_whole>\d+)\.(?P<avg_tenth>\d) "
    r"MAX(?P<max_whole>\d+)\.(?P<max_tenth>\d) "
    r"P99(?P<p99_whole>\d+)\.(?P<p99_tenth>\d) "
    r"OVR(?P<over_budget>\d+) MISS(?P<misses>\d+) "
    r"AVGUS(?P<avg_us>\d+) MAXUS(?P<max_us>\d+) "
    r"P99US(?P<p99_us>\d+) MEAVGUS(?P<me_avg_us>\d+) "
    r"MEFAULT(?P<me_faults>\d+) "
    r"H(?P<histogram>\d+(?:/\d+){9}) V(?P<valid>\d+)\Z"
)
END_RE = re.compile(r"END VALID=(?P<valid>\d+) DROP=(?P<drop>\d+)\Z")


class AuditError(ValueError):
    """The supplied evidence is not safe to use for an A/B claim."""


@dataclass(frozen=True)
class AcceptWindow:
    profile: str
    run_id: str
    window: int
    state: int
    stage: int
    frames: int
    hw_fps_x10: int
    elapsed_us: int
    avg_us: int
    max_us: int
    p99_us: int
    over_budget: int
    misses: int
    me_avg_us: int
    me_faults: int
    histogram: tuple[int, ...]
    line_number: int

    @property
    def identity(self) -> tuple[int, int, int, int]:
        return (self.window, self.state, self.stage, self.frames)


@dataclass(frozen=True)
class PerfRun:
    profile: str
    run_id: str
    windows: tuple[AcceptWindow, ...]
    end_count: int


def _decimal_x10(match: re.Match[str], stem: str) -> int:
    return int(match.group(f"{stem}_whole")) * 10 + int(match.group(f"{stem}_tenth"))


def _parse_accept(
    body: str,
    *,
    profile: str,
    run_id: str,
    window: int,
    line_number: int,
) -> AcceptWindow:
    match = ACCEPT_RE.fullmatch(body)
    if match is None:
        raise AuditError(
            f"line {line_number}: malformed or non-AB PERF ACCEPT record"
        )

    state = int(match.group("state"))
    stage = int(match.group("stage"))
    frames = int(match.group("frames"))
    hw_fps_x10 = _decimal_x10(match, "hw_fps")
    elapsed_us = int(match.group("elapsed_us"))
    avg_x10 = _decimal_x10(match, "avg")
    max_x10 = _decimal_x10(match, "max")
    p99_x10 = _decimal_x10(match, "p99")
    avg_us = int(match.group("avg_us"))
    max_us = int(match.group("max_us"))
    p99_us = int(match.group("p99_us"))
    over_budget = int(match.group("over_budget"))
    misses = int(match.group("misses"))
    me_avg_us = int(match.group("me_avg_us"))
    me_faults = int(match.group("me_faults"))
    histogram = tuple(int(value) for value in match.group("histogram").split("/"))
    valid = int(match.group("valid"))

    errors: list[str] = []
    if state != 2:
        errors.append(f"requires gameplay S2, got S{state}")
    if stage < 0:
        errors.append(f"invalid negative stage ST{stage}")
    if not 1 <= frames <= 120:
        errors.append(f"N{frames} is outside 1..120")
    if elapsed_us <= 0:
        errors.append("ELUS must be positive")
    else:
        # This is the exact integer calculation used by the PSP producer.
        expected_hw_fps_x10 = frames * 10_000_000 // elapsed_us
        if hw_fps_x10 != expected_hw_fps_x10:
            errors.append(
                f"HWFPS{hw_fps_x10 / 10:.1f} does not match "
                f"N{frames}/ELUS{elapsed_us} (expected "
                f"{expected_hw_fps_x10 / 10:.1f})"
            )
    if hw_fps_x10 <= 0 or hw_fps_x10 > 610:
        errors.append(f"HWFPS{hw_fps_x10 / 10:.1f} is outside (0, 61.0]")
    if valid != 1:
        errors.append(f"invalid window latch V{valid}")
    if me_faults != 0:
        errors.append(f"MEFAULT{me_faults} is nonzero")
    if len(histogram) != 10 or sum(histogram) != frames:
        errors.append(f"histogram sum {sum(histogram)} does not match N{frames}")
    if over_budget > frames:
        errors.append(f"OVR{over_budget} exceeds N{frames}")
    if max_us < avg_us:
        errors.append(f"MAXUS{max_us} is smaller than AVGUS{avg_us}")
    if max_us < p99_us:
        errors.append(f"MAXUS{max_us} is smaller than P99US{p99_us}")

    # Decimal fields are display copies.  AVG is formed by adding two
    # independently truncated tenths (CPU+GE), so it may trail AVGUS by up to
    # 199 us.  MAX/P99 are single values truncated to a tenth.
    avg_display_error = avg_us - avg_x10 * 100
    max_display_error = max_us - max_x10 * 100
    p99_display_error = p99_us - p99_x10 * 100
    if not 0 <= avg_display_error <= 199:
        errors.append("AVG and AVGUS are inconsistent")
    if not 0 <= max_display_error <= 99:
        errors.append("MAX and MAXUS are inconsistent")
    if not 0 <= p99_display_error <= 99:
        errors.append("P99 and P99US are inconsistent")

    if errors:
        raise AuditError(f"line {line_number}: " + "; ".join(errors))

    return AcceptWindow(
        profile=profile,
        run_id=run_id,
        window=window,
        state=state,
        stage=stage,
        frames=frames,
        hw_fps_x10=hw_fps_x10,
        elapsed_us=elapsed_us,
        avg_us=avg_us,
        max_us=max_us,
        p99_us=p99_us,
        over_budget=over_budget,
        misses=misses,
        me_avg_us=me_avg_us,
        me_faults=me_faults,
        histogram=histogram,
        line_number=line_number,
    )


def parse_log(lines: Iterable[str], expected_profile: str) -> PerfRun:
    """Parse one isolated ABME or ABSC hardware run.

    The input must contain only one AB profile and one RID.  Multiple valid END
    markers are allowed because the engine seals the RAM log at stage deletion
    and may repeat the final seal during shutdown.
    """

    if expected_profile not in AB_PROFILES:
        raise ValueError(f"unsupported expected profile: {expected_profile}")

    materialized = list(lines)
    errors: list[str] = []
    windows: list[AcceptWindow] = []
    run_ids: set[str] = set()
    end_count = 0
    last_window = 0

    for line_number, raw_line in enumerate(materialized, 1):
        line = raw_line.strip()
        if LEGACY_FPS_RE.search(line):
            errors.append(
                f"line {line_number}: forbidden legacy SHIKIGAMI FPS= evidence"
            )
        if CUR_FPS_RE.search(line):
            errors.append(f"line {line_number}: forbidden curFps evidence")
        if PERF_OVERFLOW_RE.search(line):
            errors.append(f"line {line_number}: PERF log overflow")

        tagged = TAGGED_PERF_RE.search(line)
        if tagged is None:
            if re.search(r"\bPERF\s+PF", line):
                errors.append(f"line {line_number}: malformed tagged PERF record")
            continue

        profile, raw_run_id, raw_window, body = tagged.groups()
        run_id = raw_run_id.upper()
        window = int(raw_window)
        if profile != expected_profile:
            errors.append(
                f"line {line_number}: expected PF{expected_profile}, got PF{profile}"
            )
            continue
        run_ids.add(run_id)

        if body.startswith("ACCEPT "):
            try:
                parsed = _parse_accept(
                    body,
                    profile=profile,
                    run_id=run_id,
                    window=window,
                    line_number=line_number,
                )
            except AuditError as exc:
                errors.append(str(exc))
                continue
            if window <= last_window:
                errors.append(
                    f"line {line_number}: window W{window} is duplicate or not increasing"
                )
                continue
            last_window = window
            windows.append(parsed)
            continue

        end_match = END_RE.fullmatch(body)
        if end_match is not None:
            end_count += 1
            if int(end_match.group("valid")) != 1 or int(end_match.group("drop")) != 0:
                errors.append(
                    f"line {line_number}: END must be VALID=1 DROP=0"
                )
            if last_window == 0:
                errors.append(f"line {line_number}: END precedes every ACCEPT window")
            elif window != last_window:
                errors.append(
                    f"line {line_number}: END W{window} does not seal latest W{last_window}"
                )
            continue

        errors.append(
            f"line {line_number}: PF{expected_profile} permits only ACCEPT and END records"
        )

    if not windows:
        errors.append(f"missing PF{expected_profile} PERF ACCEPT windows")
    if len(run_ids) != 1:
        errors.append(
            f"PF{expected_profile} input must contain exactly one RID, got "
            f"{sorted(run_ids)}"
        )
    if end_count == 0:
        errors.append("missing PERF END marker")
    if windows and not any(window.frames == 120 for window in windows):
        errors.append("run has no complete N120 timing window")

    if errors:
        raise AuditError("\n".join(errors))

    return PerfRun(
        profile=expected_profile,
        run_id=next(iter(run_ids)),
        windows=tuple(windows),
        end_count=end_count,
    )


def _rate(numerator: int, denominator: int) -> float:
    return round(numerator / denominator, 6) if denominator else 0.0


def _aggregate(windows: Sequence[AcceptWindow]) -> dict[str, object]:
    frames = sum(window.frames for window in windows)
    elapsed_us = sum(window.elapsed_us for window in windows)
    weighted_avg_us = sum(window.avg_us * window.frames for window in windows)
    weighted_me_us = sum(window.me_avg_us * window.frames for window in windows)
    over_budget = sum(window.over_budget for window in windows)
    misses = sum(window.misses for window in windows)
    return {
        "windows": len(windows),
        "frames": frames,
        "elapsed_us": elapsed_us,
        # This is the sole aggregate actual-FPS calculation.
        "actual_fps": round(frames * 1_000_000 / elapsed_us, 6),
        "avg_us": round(weighted_avg_us / frames, 3),
        "worst_window_p99_us": max(window.p99_us for window in windows),
        "max_us": max(window.max_us for window in windows),
        "over_budget_frames": over_budget,
        "over_budget_rate": _rate(over_budget, frames),
        "vsync_misses": misses,
        "vsync_misses_per_frame": _rate(misses, frames),
        "me_avg_us": round(weighted_me_us / frames, 3),
    }


def _stage_aggregates(run: PerfRun) -> dict[str, dict[str, object]]:
    stages = sorted({window.stage for window in run.windows})
    return {
        str(stage): _aggregate(
            tuple(window for window in run.windows if window.stage == stage)
        )
        for stage in stages
    }


def _validated_replay_sha256(value: str) -> str:
    if REPLAY_SHA256_RE.fullmatch(value) is None:
        raise AuditError("replay SHA-256 must be exactly 64 hexadecimal characters")
    return value.upper()


def replay_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as replay_file:
            for chunk in iter(lambda: replay_file.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise AuditError(f"cannot read replay {path}: {exc}") from exc
    return digest.hexdigest().upper()


def compare_runs(me_run: PerfRun, sc_run: PerfRun, replay_hash: str) -> dict[str, object]:
    replay_hash = _validated_replay_sha256(replay_hash)
    errors: list[str] = []
    if me_run.profile != AB_ME_PROFILE:
        errors.append(f"ME input must be PF{AB_ME_PROFILE}, got PF{me_run.profile}")
    if sc_run.profile != AB_SC_PROFILE:
        errors.append(f"SC input must be PF{AB_SC_PROFILE}, got PF{sc_run.profile}")
    if me_run.run_id == sc_run.run_id:
        errors.append("ME and SC inputs have the same RID/process nonce")
    if len(me_run.windows) != len(sc_run.windows):
        errors.append(
            f"window count differs: ME={len(me_run.windows)} SC={len(sc_run.windows)}"
        )

    for ordinal, (me_window, sc_window) in enumerate(
        zip(me_run.windows, sc_run.windows), 1
    ):
        if me_window.identity != sc_window.identity:
            errors.append(
                f"paired window {ordinal} identity differs: "
                f"ME{me_window.identity} SC{sc_window.identity}"
            )

    if not any(window.me_avg_us > 0 for window in me_run.windows):
        errors.append("PFABME has no window with measured ME work (MEAVGUS>0)")
    nonzero_sc_me = [window.window for window in sc_run.windows if window.me_avg_us != 0]
    if nonzero_sc_me:
        errors.append(f"PFABSC contains nonzero MEAVGUS in windows {nonzero_sc_me}")

    if errors:
        raise AuditError("\n".join(errors))

    me_summary = _aggregate(me_run.windows)
    sc_summary = _aggregate(sc_run.windows)
    me_fps = float(me_summary["actual_fps"])
    sc_fps = float(sc_summary["actual_fps"])
    me_avg_us = float(me_summary["avg_us"])
    sc_avg_us = float(sc_summary["avg_us"])

    return {
        "valid": True,
        "replay_sha256": replay_hash,
        "fps_source": "sum(N) * 1000000 / sum(ELUS)",
        "hwfps_role": "per-window N/ELUS integrity check only",
        "legacy_fps_policy": "FPS= and curFps are rejected",
        "paired_identity": "W,S,ST,N",
        "me": {
            "profile": me_run.profile,
            "run_id": me_run.run_id,
            "summary": me_summary,
            "stages": _stage_aggregates(me_run),
        },
        "sc": {
            "profile": sc_run.profile,
            "run_id": sc_run.run_id,
            "summary": sc_summary,
            "stages": _stage_aggregates(sc_run),
        },
        "delta_me_minus_sc": {
            "actual_fps": round(me_fps - sc_fps, 6),
            "actual_fps_percent": round((me_fps / sc_fps - 1.0) * 100.0, 6),
            "avg_us": round(me_avg_us - sc_avg_us, 3),
            "worst_window_p99_us": int(me_summary["worst_window_p99_us"])
            - int(sc_summary["worst_window_p99_us"]),
            "max_us": int(me_summary["max_us"]) - int(sc_summary["max_us"]),
            "over_budget_frames": int(me_summary["over_budget_frames"])
            - int(sc_summary["over_budget_frames"]),
            "vsync_misses": int(me_summary["vsync_misses"])
            - int(sc_summary["vsync_misses"]),
        },
    }


def _read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise AuditError(f"cannot read log {path}: {exc}") from exc


def compare_files(me_log: Path, sc_log: Path, replay_hash: str) -> dict[str, object]:
    me_run = parse_log(_read_lines(me_log), AB_ME_PROFILE)
    sc_run = parse_log(_read_lines(sc_log), AB_SC_PROFILE)
    return compare_runs(me_run, sc_run, replay_hash)


def _format_text(result: dict[str, object]) -> str:
    me = result["me"]
    sc = result["sc"]
    delta = result["delta_me_minus_sc"]
    assert isinstance(me, dict) and isinstance(sc, dict) and isinstance(delta, dict)
    me_summary = me["summary"]
    sc_summary = sc["summary"]
    assert isinstance(me_summary, dict) and isinstance(sc_summary, dict)
    rows = [
        "RID30 A/B VALID",
        f"replay SHA256: {result['replay_sha256']}",
        f"ME: PF{me['profile']} RID{me['run_id']}",
        f"SC: PF{sc['profile']} RID{sc['run_id']}",
        "metric                         ME             SC       ME-SC",
        f"actual FPS (sum N/sum ELUS)  {float(me_summary['actual_fps']):10.3f} "
        f"{float(sc_summary['actual_fps']):10.3f} "
        f"{float(delta['actual_fps']):+10.3f}",
        f"AVGUS                         {float(me_summary['avg_us']):10.1f} "
        f"{float(sc_summary['avg_us']):10.1f} "
        f"{float(delta['avg_us']):+10.1f}",
        f"worst P99US                   {int(me_summary['worst_window_p99_us']):10d} "
        f"{int(sc_summary['worst_window_p99_us']):10d} "
        f"{int(delta['worst_window_p99_us']):+10d}",
        f"MAXUS                         {int(me_summary['max_us']):10d} "
        f"{int(sc_summary['max_us']):10d} "
        f"{int(delta['max_us']):+10d}",
        f"OVR frames                    {int(me_summary['over_budget_frames']):10d} "
        f"{int(sc_summary['over_budget_frames']):10d} "
        f"{int(delta['over_budget_frames']):+10d}",
        f"MISS                          {int(me_summary['vsync_misses']):10d} "
        f"{int(sc_summary['vsync_misses']):10d} "
        f"{int(delta['vsync_misses']):+10d}",
        f"MEAVGUS                       {float(me_summary['me_avg_us']):10.1f} "
        f"{float(sc_summary['me_avg_us']):10.1f}",
    ]
    return "\n".join(rows)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Compare isolated PSP RID30 PFABME/PFABSC logs. Aggregate actual "
            "FPS comes only from N and ELUS."
        )
    )
    parser.add_argument("me_log", type=Path, help="isolated PFABME BOOT/PERF log")
    parser.add_argument("sc_log", type=Path, help="isolated PFABSC BOOT/PERF log")
    replay_group = parser.add_mutually_exclusive_group(required=True)
    replay_group.add_argument(
        "--replay",
        type=Path,
        help="local copy of the exact .rpy used for both runs (SHA-256 is computed)",
    )
    replay_group.add_argument(
        "--replay-sha256",
        help="recorded SHA-256 of the exact .rpy used for both runs",
    )
    parser.add_argument("--json", action="store_true", help="emit JSON")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)
    try:
        replay_hash = (
            replay_sha256(args.replay)
            if args.replay is not None
            else _validated_replay_sha256(args.replay_sha256)
        )
        result = compare_files(args.me_log, args.sc_log, replay_hash)
    except AuditError as exc:
        print(f"RID30 A/B INVALID: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(_format_text(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

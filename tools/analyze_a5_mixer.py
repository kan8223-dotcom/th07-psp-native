#!/usr/bin/env python3
"""Validate PFA5M mixer-observer logs and screen MISS target windows.

The A5-MEASURE build is observer evidence, not a performance candidate.  This
tool therefore reports only the optimistic, whole-mixer-cost upper bound
``MU/N``.  A window is a target when it has a positive P99 deficit, at least
one MISS, and removing the *entire* measured mixer cost could cover that
deficit.  No actual saving is inferred from this screen.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

try:
    from compare_rid30_ab import (
        ACCEPT_RE,
        END_RE,
        LEGACY_FPS_RE,
        CUR_FPS_RE,
        PERF_OVERFLOW_RE,
        TAGGED_PERF_RE,
        AuditError,
        _parse_accept,
    )
except ModuleNotFoundError:  # Imported as tools.analyze_a5_mixer in tests.
    from tools.compare_rid30_ab import (
        ACCEPT_RE,
        END_RE,
        LEGACY_FPS_RE,
        CUR_FPS_RE,
        PERF_OVERFLOW_RE,
        TAGGED_PERF_RE,
        AuditError,
        _parse_accept,
    )


PROFILE = "A5M"
FRAME_BUDGET_US = 16667
UINT32_MAX = (1 << 32) - 1
INT32_MAX = (1 << 31) - 1
INT32_MIN = -(1 << 31)
MIX_SAMPLE_CAPACITY = 512
SFX_VOICE_CAPACITY = 16
SAMPLES_PER_MIX_CALL = 512 * 2
EFFECT_CAPACITY = 400 + 8

# Exact producer grammar.  Unsigned conversions are range-checked separately;
# Python integers otherwise accept values that PSP's ``%u`` cannot emit.
A5M_RE = re.compile(
    r"A5M "
    r"S(?P<state>-?\d+) ST(?P<stage>-?\d+) N(?P<frames>\d+) "
    r"MU(?P<mix_total_us>\d+) MC(?P<mix_calls>\d+) "
    r"MA(?P<mix_average_us>\d+) MP99(?P<mix_p99_us>\d+) "
    r"MX(?P<mix_max_us>\d+) "
    r"AV(?P<active_voice_visits>\d+) AVM(?P<active_voice_max>\d+) "
    r"D1(?P<divisor_one_calls>\d+) TR(?P<trigger_count>\d+) "
    r"FX(?P<effect_current>-?\d+)/(?P<effect_max>\d+) "
    r"LIM(?P<limited_samples>\d+) OF(?P<sample_overflow>\d+) "
    r"G(?P<integrity>\d+)\Z"
)

UNSIGNED_A5M_FIELDS = (
    "frames",
    "mix_total_us",
    "mix_calls",
    "mix_average_us",
    "mix_p99_us",
    "mix_max_us",
    "active_voice_visits",
    "active_voice_max",
    "divisor_one_calls",
    "trigger_count",
    "effect_max",
    "limited_samples",
    "sample_overflow",
    "integrity",
)


def _parse_a5m(body: str, line_number: int, errors: list[str]) -> dict[str, int] | None:
    match = A5M_RE.fullmatch(body)
    if match is None:
        errors.append(f"line {line_number}: malformed PFA5M A5M record")
        return None

    values = {name: int(raw) for name, raw in match.groupdict().items()}
    for name in UNSIGNED_A5M_FIELDS:
        if values[name] > UINT32_MAX:
            errors.append(
                f"line {line_number}: A5M {name} exceeds uint32 ({values[name]})"
            )
    for name in ("state", "stage", "effect_current"):
        if not INT32_MIN <= values[name] <= INT32_MAX:
            errors.append(
                f"line {line_number}: A5M {name} exceeds int32 ({values[name]})"
            )
    return values


def _validate_a5m_semantics(
    values: dict[str, int], line_number: int, errors: list[str]
) -> None:
    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(f"line {line_number}: {message}")

    calls = values["mix_calls"]
    total = values["mix_total_us"]
    average = values["mix_average_us"]
    p99 = values["mix_p99_us"]
    maximum = values["mix_max_us"]
    voice_visits = values["active_voice_visits"]
    voice_max = values["active_voice_max"]
    limited = values["limited_samples"]

    require(values["integrity"] == 1, "A5M integrity G must be 1")
    require(values["sample_overflow"] == 0, "A5M sample overflow OF must be 0")
    require(
        calls == values["divisor_one_calls"],
        "A5M divisor-one closure MC != D1",
    )
    require(
        calls <= MIX_SAMPLE_CAPACITY,
        f"A5M MC exceeds {MIX_SAMPLE_CAPACITY}-sample timing capacity",
    )

    if calls == 0:
        require(
            total == average == p99 == maximum == 0,
            "A5M zero-call timing fields must all be zero",
        )
        require(
            voice_visits == voice_max == 0,
            "A5M zero-call voice fields must both be zero",
        )
        require(limited == 0, "A5M zero-call LIM must be zero")
    else:
        require(total > 0, "A5M nonzero MC requires nonzero MU")
        require(average > 0, "A5M nonzero MC requires nonzero MA")
        require(p99 > 0, "A5M nonzero MC requires nonzero MP99")
        require(maximum > 0, "A5M nonzero MC requires nonzero MX")
        require(average == total // calls, "A5M MA must equal floor(MU/MC)")
        require(maximum <= total, "A5M MX exceeds MU")
        require(total <= calls * maximum, "A5M MU exceeds MC*MX")
        require(p99 <= maximum, "A5M MP99 exceeds MX")

        require(
            1 <= voice_max <= SFX_VOICE_CAPACITY,
            f"A5M AVM must be within 1..{SFX_VOICE_CAPACITY}",
        )
        require(voice_visits >= calls, "A5M AV is below one voice per mix call")
        require(voice_visits >= voice_max, "A5M AV is below AVM")
        require(
            voice_visits <= calls * voice_max,
            "A5M AV exceeds MC*AVM",
        )
        require(
            voice_visits >= voice_max + calls - 1,
            "A5M AV cannot contain AVM plus one voice in every other call",
        )
        require(
            limited <= calls * SAMPLES_PER_MIX_CALL,
            "A5M LIM exceeds 1024 samples per mix call",
        )

    effect_current = values["effect_current"]
    effect_max = values["effect_max"]
    require(
        0 <= effect_current <= effect_max,
        "A5M FX current must be within 0..FX max",
    )
    require(
        effect_max <= EFFECT_CAPACITY,
        f"A5M FX max exceeds EffectManager capacity {EFFECT_CAPACITY}",
    )
    # TR is a request proxy written by the game thread.  It is deliberately
    # not closed against asynchronous audio-thread MC/AV counters.


def _window_result(accept: object, sample: dict[str, int]) -> dict[str, object]:
    frames = int(getattr(accept, "frames"))
    p99_us = int(getattr(accept, "p99_us"))
    misses = int(getattr(accept, "misses"))
    total_us = sample["mix_total_us"]
    deficit_us = max(p99_us - FRAME_BUDGET_US, 0)
    covers_deficit = deficit_us > 0 and total_us >= deficit_us * frames
    is_target = misses > 0 and covers_deficit
    upper_bound = total_us / frames
    miss_density = misses / frames
    priority = miss_density * deficit_us
    return {
        "window": int(getattr(accept, "window")),
        "state": int(getattr(accept, "state")),
        "stage": int(getattr(accept, "stage")),
        "frames": frames,
        "p99_us": p99_us,
        "p99_deficit_us": deficit_us,
        "misses": misses,
        "miss_density": round(miss_density, 9),
        "mixer_total_us": total_us,
        "mixer_calls": sample["mix_calls"],
        "mixer_whole_cost_upper_bound_us_per_frame": round(upper_bound, 6),
        "upper_bound_minus_deficit_us": round(upper_bound - deficit_us, 6),
        "upper_bound_covers_p99_deficit": covers_deficit,
        "target": is_target,
        "priority_miss_density_x_deficit": round(priority, 6),
        "mixer_average_us_per_call": sample["mix_average_us"],
        "mixer_p99_us_per_call": sample["mix_p99_us"],
        "mixer_max_us_per_call": sample["mix_max_us"],
        "active_voice_visits": sample["active_voice_visits"],
        "active_voice_max": sample["active_voice_max"],
        "trigger_count": sample["trigger_count"],
        "effect_current": sample["effect_current"],
        "effect_max": sample["effect_max"],
        "limited_samples": sample["limited_samples"],
    }


def analyze(lines: Iterable[str]) -> dict[str, object]:
    """Analyze one complete PFA5M run without raising for evidence faults."""

    errors: list[str] = []
    accepts: dict[int, object] = {}
    samples: dict[int, dict[str, int]] = {}
    identities: set[tuple[str, str]] = set()
    last_accept_window = 0
    pending_accept: object | None = None
    pending_accept_line: int | None = None
    last_tagged_kind: str | None = None
    end_count = 0

    for line_number, raw_line in enumerate(lines, 1):
        line = raw_line.strip()
        if PERF_OVERFLOW_RE.search(line):
            errors.append(f"line {line_number}: PERF log overflow")
        if LEGACY_FPS_RE.search(line):
            errors.append(f"line {line_number}: legacy FPS= telemetry is forbidden")
        if CUR_FPS_RE.search(line):
            errors.append(f"line {line_number}: curFps telemetry is forbidden")

        tagged = TAGGED_PERF_RE.search(line)
        if tagged is None:
            if re.search(r"\bPERF\s+PF\S+\s+RID\S+\s+W\S+", line):
                errors.append(f"line {line_number}: malformed tagged PERF record")
                last_tagged_kind = "malformed"
            continue

        profile = tagged.group(1)
        run_id = tagged.group(2).upper()
        window = int(tagged.group(3))
        body = tagged.group(4)
        identities.add((profile, run_id))

        if profile != PROFILE:
            errors.append(
                f"line {line_number}: expected PF{PROFILE}, got PF{profile}"
            )
            last_tagged_kind = "foreign"
            continue

        if body.startswith("ACCEPT"):
            if pending_accept is not None:
                errors.append(
                    f"line {line_number}: ACCEPT W{window} arrived before A5M "
                    f"for W{getattr(pending_accept, 'window')}"
                )
            if ACCEPT_RE.fullmatch(body) is None:
                errors.append(
                    f"line {line_number}: malformed or non-AB PERF ACCEPT record"
                )
                pending_accept = None
                pending_accept_line = None
                last_tagged_kind = "accept-invalid"
                continue
            try:
                accept = _parse_accept(
                    body,
                    profile=profile,
                    run_id=run_id,
                    window=window,
                    line_number=line_number,
                )
            except AuditError as error:
                errors.append(str(error))
                pending_accept = None
                pending_accept_line = None
                last_tagged_kind = "accept-invalid"
                continue
            if window in accepts:
                errors.append(f"line {line_number}: duplicate ACCEPT W{window}")
            if window <= last_accept_window:
                errors.append(
                    f"line {line_number}: ACCEPT W{window} is not strictly increasing"
                )
            else:
                last_accept_window = window
            accepts.setdefault(window, accept)
            pending_accept = accept
            pending_accept_line = line_number
            last_tagged_kind = "accept"
            continue

        if body.startswith("A5M"):
            values = _parse_a5m(body, line_number, errors)
            if (
                last_tagged_kind != "accept"
                or pending_accept is None
                or pending_accept_line != line_number - 1
            ):
                errors.append(
                    f"line {line_number}: A5M must immediately follow its ACCEPT"
                )
            if window in samples:
                errors.append(f"line {line_number}: duplicate A5M W{window}")
            if values is not None:
                _validate_a5m_semantics(values, line_number, errors)
                if pending_accept is not None:
                    expected = (
                        int(getattr(pending_accept, "window")),
                        int(getattr(pending_accept, "state")),
                        int(getattr(pending_accept, "stage")),
                        int(getattr(pending_accept, "frames")),
                    )
                    actual = (
                        window,
                        values["state"],
                        values["stage"],
                        values["frames"],
                    )
                    if actual != expected:
                        errors.append(
                            f"line {line_number}: A5M W/S/ST/N {actual} does not "
                            f"match ACCEPT {expected}"
                        )
                samples.setdefault(window, values)
            pending_accept = None
            pending_accept_line = None
            last_tagged_kind = "a5m"
            continue

        end_match = END_RE.fullmatch(body)
        if end_match is not None:
            if pending_accept is not None:
                errors.append(
                    f"line {line_number}: END arrived before A5M for "
                    f"W{getattr(pending_accept, 'window')}"
                )
                pending_accept = None
                pending_accept_line = None
            end_count += 1
            if int(end_match.group("valid")) != 1 or int(end_match.group("drop")) != 0:
                errors.append(f"line {line_number}: END must be VALID=1 DROP=0")
            if last_accept_window == 0:
                errors.append(f"line {line_number}: END precedes every ACCEPT window")
            elif window != last_accept_window:
                errors.append(
                    f"line {line_number}: END W{window} does not seal latest "
                    f"W{last_accept_window}"
                )
            last_tagged_kind = "end"
            continue

        errors.append(
            f"line {line_number}: PF{PROFILE} permits only ACCEPT, A5M, and END records"
        )
        last_tagged_kind = "other"

    if pending_accept is not None:
        errors.append(f"missing A5M for ACCEPT W{getattr(pending_accept, 'window')}")
    if not accepts:
        errors.append(f"missing PF{PROFILE} PERF ACCEPT windows")
    if len(identities) != 1:
        errors.append(
            "input must contain exactly one PF/RID identity, got "
            f"{sorted(identities)}"
        )
    elif next(iter(identities))[0] != PROFILE:
        errors.append(f"input identity must use PF{PROFILE}")
    if end_count == 0:
        errors.append("missing PERF END marker")
    if last_tagged_kind != "end":
        errors.append("final tagged PERF record must be END")
    if accepts and not any(int(getattr(window, "frames")) == 120 for window in accepts.values()):
        errors.append("run has no complete N120 timing window")

    accept_windows = set(accepts)
    sample_windows = set(samples)
    for window in sorted(accept_windows - sample_windows):
        errors.append(f"missing A5M for ACCEPT W{window}")
    for window in sorted(sample_windows - accept_windows):
        errors.append(f"A5M W{window} has no ACCEPT")

    rows = [
        _window_result(accepts[window], samples[window])
        for window in sorted(accept_windows & sample_windows)
    ]
    targets = sorted(
        (row for row in rows if row["target"]),
        key=lambda row: (
            -float(row["priority_miss_density_x_deficit"]),
            int(row["window"]),
        ),
    )
    profile: str | None = None
    run_id: str | None = None
    if len(identities) == 1:
        profile, run_id = next(iter(identities))

    return {
        "valid": not errors,
        "profile": profile,
        "run_id": run_id,
        "accept_windows": len(accepts),
        "a5m_windows": len(samples),
        "end_markers": end_count,
        "frame_budget_us": FRAME_BUDGET_US,
        "upper_bound_definition": "MU/N (entire measured mixer cost)",
        "target_definition": "MISS>0 and P99US>16667 and MU/N >= P99US-16667",
        "performance_claim": False,
        "windows": rows,
        "target_windows": targets,
        "errors": errors,
    }


def _read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        raise AuditError(f"cannot read {path}: {error}") from error


def _format_text(result: dict[str, object]) -> str:
    rows = [
        f"A5-MEASURE valid={int(bool(result['valid']))} "
        f"PF{result['profile']} RID{result['run_id']} "
        f"accept={result['accept_windows']} a5m={result['a5m_windows']} "
        f"targets={len(result['target_windows'])}",
        "performance claim: disabled (observer whole-cost upper bound only)",
    ]
    for target in result["target_windows"]:
        rows.append(
            f"TARGET W{target['window']} ST{target['stage']} N{target['frames']} "
            f"MISS{target['misses']} P99DEF{target['p99_deficit_us']}us "
            f"MUB{target['mixer_whole_cost_upper_bound_us_per_frame']:.3f}us/f "
            f"HEAD{target['upper_bound_minus_deficit_us']:.3f}us"
        )
    return "\n".join(rows)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate one PFA5M hardware log and screen MISS windows."
    )
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)
    try:
        result = analyze(_read_lines(args.log))
    except AuditError as error:
        print(f"A5-MEASURE INVALID: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(_format_text(result))
        for error in result["errors"]:
            print(f"ERROR: {error}", file=sys.stderr)
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

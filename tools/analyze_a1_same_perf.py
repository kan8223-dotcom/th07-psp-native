#!/usr/bin/env python3
"""Validate and summarize sparse A1-SAME records from one PFABME run.

This tool checks observer attribution and side-effect closure.  It deliberately
does not turn the instrumented run into an ME-vs-SC performance claim.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence, cast

try:
    from analyze_final60_perf import A1_SAME_LABELS, _validate_a1s_line
    from compare_rid30_ab import (
        AB_ME_PROFILE,
        ACCEPT_RE,
        AuditError,
        TAGGED_PERF_RE,
        parse_log,
    )
except ModuleNotFoundError:  # Imported as tools.analyze_a1_same_perf in tests.
    from tools.analyze_final60_perf import A1_SAME_LABELS, _validate_a1s_line
    from tools.compare_rid30_ab import (
        AB_ME_PROFILE,
        ACCEPT_RE,
        AuditError,
        TAGGED_PERF_RE,
        parse_log,
    )


REASON_BITS = {
    "UNKNOWN": 1 << 0,
    "BEGIN_SPELL": 1 << 1,
    "SPELL_END": 1 << 2,
    "BOSS_DEFEAT": 1 << 3,
    "SPELL_TIMEOUT": 1 << 4,
    "ECL_ENEMY_CLEAR": 1 << 5,
    "ECL_BULLET_ITEM": 1 << 6,
    "ECL_RADIUS": 1 << 7,
    "ECL_BULLET_FADE": 1 << 8,
    "DIALOGUE": 1 << 9,
    "RESPAWN_GRACE": 1 << 10,
    "FULL_POWER": 1 << 11,
    "BOMB": 1 << 12,
}
SUM_FIELDS = (
    "calls",
    "elapsed_us",
    "eligible",
    "affected",
    "item_attempts",
    "popups",
    "auxiliary",
)

# The frozen RID30 A/B parser deliberately accepts only its original compact
# ACCEPT record.  A1-SAME is a one-delta observer on top of D2B, whose ACCEPT
# record has this additional, fixed-shape position-SoA block.  Keep support for
# that block local to this analyzer instead of weakening the shared A/B parser.
D2B_ACCEPT_RE = re.compile(
    r"(?P<prefix>ACCEPT .+? MEFAULT\d+) "
    r"PSV(?P<active_visits>\d+) PSM(?P<matches>\d+) "
    r"PSC(?P<not_valid>\d+) PSWD(?P<would_defer>\d+) "
    r"PSWN(?P<unsupported_matches>\d+) "
    r"PSWU(?P<would_materialize_unsupported>\d+) "
    r"PSP(?P<publishes>\d+) PSS(?P<spawn_publishes>\d+) "
    r"PSI(?P<invalidations>\d+) "
    r"PSMV(?P<mutation_visits>\d+) PSMM(?P<mutation_matches>\d+) "
    r"PSMC(?P<mutation_not_valid>\d+) "
    r"PSMD(?P<mutation_deferred>\d+) "
    r"PSMK(?P<mutation_canonical>\d+) "
    r"PSMF(?P<mutation_faults>\d+) "
    r"PSMR(?P<mutation_bulk_clear_item>\d+)/"
    r"(?P<mutation_despawn_transition>\d+)/"
    r"(?P<mutation_bulk_despawn>\d+)/"
    r"(?P<mutation_radius_query>\d+) "
    r"PSX(?P<manager_mismatch>\d+)/(?P<generation_mismatch>\d+)/"
    r"(?P<calc_mismatch>\d+)/(?P<position_mismatch>\d+)/"
    r"(?P<invalid_slot>\d+)/(?P<publish_rejected>\d+) "
    r"PSB(?P<pause_clears>\d+)/(?P<demo_restart_clears>\d+) "
    r"PSBM(?P<would_materialize_pause>\d+)/"
    r"(?P<would_materialize_demo_restart>\d+) "
    r"PSR(?P<manager_resets>\d+)/(?P<calc_passes>\d+) "
    r"PSVC(?P<valid_slots>\d+) PSG(?P<position_valid>\d+) "
    r"PSRA(?P<read_attempts>\d+) PSRH(?P<read_hits>\d+) "
    r"PSRF(?P<read_fallbacks>\d+) "
    r"PSRX(?P<read_faults>\d+)/(?P<read_disabled>\d+)/"
    r"(?P<readable_calc_serial>\d+) "
    r"PSME(?P<me_soa_jobs>\d+)/(?P<me_aos_jobs>\d+) "
    r"(?P<suffix>H\d+(?:/\d+){9} V\d+)\Z"
)

D2B_ERROR_FIELDS = (
    "manager_mismatch",
    "generation_mismatch",
    "calc_mismatch",
    "position_mismatch",
    "invalid_slot",
    "publish_rejected",
)
A1S_BODY_RE = re.compile(
    rf"A1S K[0-9A-Fa-f]{{2}}"
    rf"(?: (?:{'|'.join(A1_SAME_LABELS)})"
    rf"\d+(?:/\d+){{6}}/[0-9A-Fa-f]{{8}}/[0-9A-Fa-f]{{8}}){{1,5}}"
    rf" G\d+ O\d+\Z"
)


def _normalize_d2b_accept(
    body: str, line_number: int, errors: list[str]
) -> tuple[str, dict[str, int] | None]:
    """Validate D2B's extension and return the frozen AB base record."""

    if ACCEPT_RE.fullmatch(body) is not None:
        return body, None
    match = D2B_ACCEPT_RE.fullmatch(body)
    if match is None:
        return body, None

    values = {
        name: int(raw)
        for name, raw in match.groupdict().items()
        if name not in {"prefix", "suffix"}
    }

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(f"line {line_number}: {message}")

    classified = (
        values["matches"]
        + values["not_valid"]
        + sum(values[name] for name in D2B_ERROR_FIELDS[:5])
    )
    require(
        classified == values["active_visits"],
        "D2B active-visit classification does not close",
    )
    require(
        values["would_defer"] + values["unsupported_matches"]
        == values["matches"],
        "D2B match classification does not close",
    )
    require(
        values["would_materialize_unsupported"]
        <= values["unsupported_matches"] + values["would_defer"],
        "D2B unsupported materialization exceeds eligible matches",
    )
    require(
        values["mutation_matches"]
        + values["mutation_not_valid"]
        + values["mutation_faults"]
        == values["mutation_visits"],
        "D2B mutation visit classification does not close",
    )
    require(
        values["mutation_deferred"] + values["mutation_canonical"]
        == values["mutation_matches"],
        "D2B mutation match classification does not close",
    )
    require(
        values["mutation_bulk_clear_item"]
        + values["mutation_despawn_transition"]
        + values["mutation_bulk_despawn"]
        + values["mutation_radius_query"]
        == values["mutation_deferred"],
        "D2B deferred-mutation reasons do not close",
    )
    require(
        values["spawn_publishes"] <= values["publishes"],
        "D2B spawn publishes exceed all publishes",
    )
    require(values["valid_slots"] <= 1024, "D2B valid slots exceed capacity")
    require(values["mutation_faults"] == 0, "D2B mutation faults are nonzero")
    for name in D2B_ERROR_FIELDS:
        require(values[name] == 0, f"D2B {name} is nonzero")
    require(values["position_valid"] == 1, "D2B position closure PSG is not 1")
    require(
        values["read_hits"] + values["read_fallbacks"]
        == values["read_attempts"],
        "D2B read accounting PSRH+PSRF != PSRA",
    )
    require(values["read_faults"] == 0, "D2B read faults are nonzero")
    require(values["read_disabled"] == 0, "D2B reader disabled is nonzero")

    return f"{match.group('prefix')} {match.group('suffix')}", values


def _validate_a1_sample_semantics(
    sample: dict[str, object], frames: int, errors: list[str]
) -> None:
    tuples = sample["tuples"]
    assert isinstance(tuples, dict)
    line_number = int(sample["line_number"])

    def values(label: str) -> dict[str, int]:
        parsed = tuples[label]
        assert isinstance(parsed, dict)
        return cast(dict[str, int], parsed)

    for label in A1_SAME_LABELS:
        entry = values(label)
        if entry["reasons"].bit_count() > entry["calls"]:
            errors.append(
                f"line {line_number}: A1S {label} has more reason bits than calls"
            )
        primary_modes = (
            entry["modes"] & 0x7 if label == "RAE" else entry["modes"]
        )
        if primary_modes.bit_count() > entry["calls"]:
            errors.append(
                f"line {line_number}: A1S {label} has more mode bits than calls"
            )

    rab = values("RAB")
    if rab["affected"] != rab["eligible"]:
        errors.append(f"line {line_number}: A1S RAB affected must equal eligible")
    if rab["popups"] != 0:
        errors.append(f"line {line_number}: A1S RAB popups must be zero")
    if (rab["modes"] & 0x2) == 0 and rab["item_attempts"] != 0:
        errors.append(
            f"line {line_number}: A1S RAB has item attempts without item mode"
        )
    if rab["modes"] == 0x2 and rab["item_attempts"] < rab["affected"]:
        errors.append(
            f"line {line_number}: A1S RAB item-mode attempts are below affected bullets"
        )

    dsp = values("DSP")
    if dsp["affected"] != dsp["eligible"]:
        errors.append(f"line {line_number}: A1S DSP affected must equal eligible")
    if dsp["item_attempts"] < dsp["affected"] + dsp["auxiliary"]:
        errors.append(
            f"line {line_number}: A1S DSP item attempts are below bullet/laser minimum"
        )
    if dsp["affected"] != 0 and dsp["popups"] == 0:
        errors.append(
            f"line {line_number}: A1S DSP affected bullets have no popup attempts"
        )

    rae = values("RAE")
    if rae["calls"] != 0 and (rae["modes"] & 0x7) == 0:
        errors.append(
            f"line {line_number}: A1S RAE has no primary score mode"
        )
    if rae["auxiliary"] > rae["affected"]:
        errors.append(
            f"line {line_number}: A1S RAE projectile enemies exceed affected enemies"
        )
    if rae["auxiliary"] > rae["item_attempts"]:
        errors.append(
            f"line {line_number}: A1S RAE projectile enemies exceed item attempts"
        )

    bup = values("BUP")
    if bup["calls"] > 2 * frames + 1:
        errors.append(
            f"line {line_number}: A1S BUP calls {bup['calls']} exceed 2*N{frames}+1"
        )


def analyze(
    lines: Iterable[str], required_reasons: Iterable[str] = ()
) -> dict[str, object]:
    materialized = list(lines)
    base_lines = list(materialized)
    errors: list[str] = []
    samples: list[dict[str, object]] = []
    d2b_samples: list[dict[str, int | str]] = []
    last_tagged: tuple[str, str, int, str] | None = None
    last_tagged_line_number: int | None = None

    for line_number, raw_line in enumerate(materialized, 1):
        line = raw_line.strip()
        tagged = TAGGED_PERF_RE.search(line)
        if tagged is None:
            continue
        profile, raw_run_id, raw_window, body = tagged.groups()
        run_id = raw_run_id.upper()
        window = int(raw_window)
        if body.startswith("A1S "):
            # Preserve original line numbers in errors from the strict ABME
            # parser while removing the sparse record it intentionally rejects.
            base_lines[line_number - 1] = ""
            if profile != AB_ME_PROFILE:
                errors.append(
                    f"line {line_number}: A1S requires PF{AB_ME_PROFILE}, got PF{profile}"
                )
            if (
                last_tagged != (profile, run_id, window, "accept")
                or last_tagged_line_number != line_number - 1
            ):
                errors.append(
                    f"line {line_number}: A1S must immediately follow its ACCEPT record"
                )
            if A1S_BODY_RE.fullmatch(body) is None:
                errors.append(f"line {line_number}: malformed A1S record")
            parsed = _validate_a1s_line(raw_line.strip(), line_number, errors)
            if parsed is not None:
                parsed.update(
                    {"profile": profile, "run_id": run_id, "window": window}
                )
                samples.append(parsed)
            last_tagged = (profile, run_id, window, "a1s")
            last_tagged_line_number = line_number
            continue

        if body.startswith("ACCEPT "):
            normalized, d2b = _normalize_d2b_accept(body, line_number, errors)
            if normalized != body:
                body_start, body_end = tagged.span(4)
                base_lines[line_number - 1] = (
                    line[:body_start] + normalized + line[body_end:]
                )
            if d2b is not None:
                d2b_samples.append(
                    {
                        **d2b,
                        "line_number": line_number,
                        "profile": profile,
                        "run_id": run_id,
                        "window": window,
                    }
                )

        kind = (
            "accept"
            if body.startswith("ACCEPT ")
            else "end"
            if body.startswith("END ")
            else "other"
        )
        last_tagged = (profile, run_id, window, kind)
        last_tagged_line_number = line_number

    if last_tagged is not None and last_tagged[3] != "end":
        errors.append("final tagged PERF record must be END")

    try:
        run = parse_log(base_lines, AB_ME_PROFILE)
    except AuditError as error:
        errors.extend(str(error).splitlines())
        run = None

    if not samples:
        errors.append("missing sparse A1S event records")

    timing: dict[str, int | float] | None = None
    if run is not None:
        valid_windows = {window.window for window in run.windows}
        seen_windows: set[int] = set()
        for sample in samples:
            if sample["run_id"] != run.run_id:
                errors.append(
                    f"line {sample['line_number']}: A1S RID{sample['run_id']} "
                    f"does not match PFABME RID{run.run_id}"
                )
            window = int(sample["window"])
            if window not in valid_windows:
                errors.append(
                    f"line {sample['line_number']}: A1S W{window} has no valid ACCEPT"
                )
            if window in seen_windows:
                errors.append(f"line {sample['line_number']}: duplicate A1S W{window}")
            seen_windows.add(window)

        frames = sum(window.frames for window in run.windows)
        elapsed_us = sum(window.elapsed_us for window in run.windows)
        timing = {
            "windows": len(run.windows),
            "frames": frames,
            "elapsed_us": elapsed_us,
            "actual_fps": round(frames * 1_000_000 / elapsed_us, 6),
            "vsync_misses": sum(window.misses for window in run.windows),
            "over_budget_frames": sum(
                window.over_budget for window in run.windows
            ),
            "minimum_hwfps": min(
                window.hw_fps_x10 for window in run.windows
            )
            / 10.0,
        }

        if not d2b_samples:
            errors.append("missing D2B PSV...PSME extension on ACCEPT records")
        else:
            d2b_windows = {int(sample["window"]) for sample in d2b_samples}
            if d2b_windows != valid_windows:
                errors.append(
                    "D2B extension coverage does not match valid ACCEPT windows"
                )
            for sample in d2b_samples:
                if sample["profile"] != run.profile or sample["run_id"] != run.run_id:
                    errors.append(
                        f"line {sample['line_number']}: D2B extension identity "
                        "does not match the PFABME run"
                    )

        frames_by_window = {
            window.window: window.frames for window in run.windows
        }
        for sample in samples:
            window = int(sample["window"])
            if window in frames_by_window:
                _validate_a1_sample_semantics(
                    sample, frames_by_window[window], errors
                )

    d2b_summary: dict[str, int | float | bool] = {
        "present": bool(d2b_samples),
        "windows": len(d2b_samples),
        "active_visits": sum(
            int(sample["active_visits"]) for sample in d2b_samples
        ),
        "matches": sum(int(sample["matches"]) for sample in d2b_samples),
        "not_valid": sum(int(sample["not_valid"]) for sample in d2b_samples),
        "would_defer": sum(
            int(sample["would_defer"]) for sample in d2b_samples
        ),
        "unsupported_matches": sum(
            int(sample["unsupported_matches"]) for sample in d2b_samples
        ),
        "would_materialize_unsupported": sum(
            int(sample["would_materialize_unsupported"])
            for sample in d2b_samples
        ),
        "publishes": sum(int(sample["publishes"]) for sample in d2b_samples),
        "spawn_publishes": sum(
            int(sample["spawn_publishes"]) for sample in d2b_samples
        ),
        "mutation_visits": sum(
            int(sample["mutation_visits"]) for sample in d2b_samples
        ),
        "mutation_deferred": sum(
            int(sample["mutation_deferred"]) for sample in d2b_samples
        ),
        "mutation_canonical": sum(
            int(sample["mutation_canonical"]) for sample in d2b_samples
        ),
        "valid_slots_max": max(
            (int(sample["valid_slots"]) for sample in d2b_samples), default=0
        ),
        "read_attempts": sum(int(sample["read_attempts"]) for sample in d2b_samples),
        "read_hits": sum(int(sample["read_hits"]) for sample in d2b_samples),
        "read_fallbacks": sum(
            int(sample["read_fallbacks"]) for sample in d2b_samples
        ),
        "read_faults": sum(int(sample["read_faults"]) for sample in d2b_samples),
        "read_disabled": sum(
            int(sample["read_disabled"]) for sample in d2b_samples
        ),
        "me_soa_jobs": sum(int(sample["me_soa_jobs"]) for sample in d2b_samples),
        "me_aos_jobs": sum(int(sample["me_aos_jobs"]) for sample in d2b_samples),
    }
    d2b_summary["update_publishes"] = (
        int(d2b_summary["publishes"]) - int(d2b_summary["spawn_publishes"])
    )
    matches = int(d2b_summary["matches"])
    d2b_summary["eligible_percent"] = round(
        int(d2b_summary["would_defer"]) * 100.0 / matches, 6
    ) if matches else 0.0
    read_attempts = int(d2b_summary["read_attempts"])
    d2b_summary["read_hit_percent"] = round(
        int(d2b_summary["read_hits"]) * 100.0 / read_attempts, 6
    ) if read_attempts else 0.0
    would_defer = int(d2b_summary["would_defer"])
    d2b_summary["read_hits_per_eligible_percent"] = round(
        int(d2b_summary["read_hits"]) * 100.0 / would_defer, 6
    ) if would_defer else 0.0
    if d2b_samples and int(d2b_summary["me_soa_jobs"]) == 0:
        errors.append("D2B run has no measured SoA ME jobs (PSME first field is zero)")

    totals: dict[str, dict[str, int]] = {
        label: {**{field: 0 for field in SUM_FIELDS}, "reasons": 0, "modes": 0}
        for label in A1_SAME_LABELS
    }
    for sample in samples:
        tuples = sample["tuples"]
        assert isinstance(tuples, dict)
        for label in A1_SAME_LABELS:
            values = tuples[label]
            assert isinstance(values, dict)
            for field in SUM_FIELDS:
                totals[label][field] += int(values[field])
            totals[label]["reasons"] |= int(values["reasons"])
            totals[label]["modes"] |= int(values["modes"])

    observed_reason_mask = 0
    for values in totals.values():
        observed_reason_mask |= values["reasons"]
    observed_reasons = [
        name for name, bit in REASON_BITS.items() if observed_reason_mask & bit
    ]
    mixed_attribution: list[dict[str, object]] = []
    for sample in samples:
        tuples = sample["tuples"]
        assert isinstance(tuples, dict)
        for label in A1_SAME_LABELS:
            values = tuples[label]
            assert isinstance(values, dict)
            reasons = int(values["reasons"])
            modes = int(values["modes"])
            if reasons.bit_count() > 1 or modes.bit_count() > 1:
                mixed_attribution.append(
                    {
                        "window": sample["window"],
                        "kind": label,
                        "reasons": reasons,
                        "modes": modes,
                    }
                )
    normalized_required = [reason.upper() for reason in required_reasons]
    for reason in normalized_required:
        if reason not in REASON_BITS:
            errors.append(f"unknown required reason: {reason}")
        elif not (observed_reason_mask & REASON_BITS[reason]):
            errors.append(f"required A1S reason was not observed: {reason}")

    return {
        "valid": not errors,
        "profile": run.profile if run is not None else None,
        "run_id": run.run_id if run is not None else None,
        "accept_windows": len(run.windows) if run is not None else 0,
        "a1_same_windows": len(samples),
        "hardware_timing": timing,
        "d2b": d2b_summary,
        "observed_reasons": observed_reasons,
        "reason_attribution_complete": not mixed_attribution,
        "mixed_attribution": mixed_attribution,
        "totals": totals,
        "samples": samples,
        "performance_claim": False,
        "errors": errors,
    }


def _read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        raise AuditError(f"cannot read {path}: {error}") from error


def _format_text(result: dict[str, object]) -> str:
    rows = [
        f"A1-SAME valid={int(bool(result['valid']))} "
        f"PF{result['profile']} RID{result['run_id']} "
        f"accept={result['accept_windows']} a1s={result['a1_same_windows']}",
        "performance claim: disabled (observer run)",
        "reason attribution: "
        + ("complete" if result["reason_attribution_complete"] else "mixed-window"),
        "reasons: " + ",".join(result["observed_reasons"]),
    ]
    timing = result["hardware_timing"]
    if isinstance(timing, dict):
        rows.append(
            f"hardware: frames={timing['frames']} elapsed_us={timing['elapsed_us']} "
            f"actual_fps={timing['actual_fps']:.6f} misses={timing['vsync_misses']} "
            f"over_budget={timing['over_budget_frames']} "
            f"min_hwfps={timing['minimum_hwfps']:.1f}"
        )
    d2b = result["d2b"]
    if isinstance(d2b, dict) and d2b["present"]:
        rows.append(
            f"D2B: windows={d2b['windows']} reads={d2b['read_attempts']} "
            f"hits={d2b['read_hits']} fallback={d2b['read_fallbacks']} "
            f"hit_percent={d2b['read_hit_percent']:.6f} "
            f"fault={d2b['read_faults']} disabled={d2b['read_disabled']} "
            f"me_jobs={d2b['me_soa_jobs']}/{d2b['me_aos_jobs']}"
        )
        rows.append(
            f"D2B coverage: visits={d2b['active_visits']} "
            f"matches={d2b['matches']} eligible={d2b['would_defer']} "
            f"eligible_percent={d2b['eligible_percent']:.6f} "
            f"unsupported={d2b['unsupported_matches']} "
            f"final_revoke={d2b['would_materialize_unsupported']} "
            f"update_publish={d2b['update_publishes']} "
            f"mutation_deferred={d2b['mutation_deferred']} "
            f"hits_per_eligible_percent="
            f"{d2b['read_hits_per_eligible_percent']:.6f}"
        )
    totals = result["totals"]
    assert isinstance(totals, dict)
    for label in A1_SAME_LABELS:
        values = totals[label]
        assert isinstance(values, dict)
        rows.append(
            f"{label} calls={values['calls']} us={values['elapsed_us']} "
            f"eligible={values['eligible']} affected={values['affected']} "
            f"items={values['item_attempts']} popups={values['popups']} "
            f"aux={values['auxiliary']} reasons={values['reasons']:08X} "
            f"modes={values['modes']:08X}"
        )
    return "\n".join(rows)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate sparse A1-SAME records in one instrumented PFABME log."
    )
    parser.add_argument("log", type=Path)
    parser.add_argument(
        "--require-reason",
        action="append",
        default=[],
        choices=tuple(REASON_BITS),
        help="require an event reason (repeatable)",
    )
    parser.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)
    try:
        result = analyze(_read_lines(args.log), args.require_reason)
    except AuditError as error:
        print(f"A1-SAME INVALID: {error}", file=sys.stderr)
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

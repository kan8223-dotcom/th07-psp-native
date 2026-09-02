#!/usr/bin/env python3
"""Build a fail-closed MISS targeting map from PSP PERF ACCEPT logs.

The producer contract in ``PspGuGraphics.cpp`` is deliberately reflected here:

* one timing window contains at most 120 completed presents;
* ``AVGUS`` is the mean CPU critical path plus the GE tail after vblank;
* ``OVR`` counts samples strictly greater than the 16,667 us budget; and
* ``MISS`` is the sum of ``sceDisplayGetVcount()`` deltas minus one.

The map therefore uses ``MISS / N`` as miss density and
``max(P99US - 16667, 0)`` as a tail-shortfall proxy.  Their product is a
targeting score.  ``AVGUS - 16667`` is retained separately, but neither proxy
proves that saving that many microseconds will eliminate every miss: the
window histogram, MAX and unobserved within-bucket variance still matter.

Only exact tagged ``PERF ... ACCEPT`` records sealed by a clean ``END`` are
used.  Logs with V0, overflow, broken window identity, malformed histograms or
an unsealed tail are rejected as a whole.  A reference log must be selected
explicitly for the sniper list so a cross-build collection is never ranked or
presented as an A/B performance claim.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import sys
from dataclasses import asdict, dataclass, replace
from pathlib import Path
from typing import Sequence


FRAME_DEADLINE_US = 16_667
DEFAULT_TARGET_MIN_US = 300
DEFAULT_TARGET_MAX_US = 800
NEAR_60_MIN_AVG_US = 16_700
NEAR_60_MAX_AVG_US = 17_500

TAGGED_PERF_RE = re.compile(
    r"(?:^|\s)PERF PF(?P<profile>[A-Z0-9_]+) "
    r"RID(?P<run_id>[0-9A-Fa-f]{8}) W(?P<window>\d+) "
    r"(?P<body>.*?)\s*$"
)
ACCEPT_RE = re.compile(
    r"ACCEPT S(?P<state>-?\d+) ST(?P<stage>-?\d+) N(?P<frames>\d+) "
    r"(?:HWFPS(?P<hw_whole>\d+)\.(?P<hw_tenth>\d) "
    r"ELUS(?P<elapsed_us>\d+) )?"
    r"AVG(?P<avg_whole>\d+)\.(?P<avg_tenth>\d) "
    r"MAX(?P<max_whole>\d+)\.(?P<max_tenth>\d) "
    r"P99(?P<p99_whole>\d+)\.(?P<p99_tenth>\d) "
    r"OVR(?P<over_budget>\d+) MISS(?P<misses>\d+) "
    r"AVGUS(?P<avg_us>\d+) MAXUS(?P<max_us>\d+) "
    r"P99US(?P<p99_us>\d+)"
    r"(?P<extension>(?: [A-Z][A-Z0-9_]*(?:[^\s]*))*) "
    r"H(?P<histogram>\d+(?:/\d+){9}) V(?P<valid>\d+)\Z"
)
END_RE = re.compile(r"END VALID=(?P<valid>\d+) DROP=(?P<drop>\d+)\Z")
PERF_OVERFLOW_RE = re.compile(r"\bPERF\b.*\bOVERFLOW\b", re.IGNORECASE)
FIXED_30_RE = re.compile(r"\bfixed 30fps on\b", re.IGNORECASE)
CORE_FIELD_PATTERNS = {
    "S": re.compile(r"(?:^|\s)S-?\d+(?=\s|$)"),
    "ST": re.compile(r"(?:^|\s)ST-?\d+(?=\s|$)"),
    "N": re.compile(r"(?:^|\s)N\d+(?=\s|$)"),
    "AVG": re.compile(r"(?:^|\s)AVG\d+\.\d(?=\s|$)"),
    "MAX": re.compile(r"(?:^|\s)MAX\d+\.\d(?=\s|$)"),
    "P99": re.compile(r"(?:^|\s)P99\d+\.\d(?=\s|$)"),
    "OVR": re.compile(r"(?:^|\s)OVR\d+(?=\s|$)"),
    "MISS": re.compile(r"(?:^|\s)MISS\d+(?=\s|$)"),
    "AVGUS": re.compile(r"(?:^|\s)AVGUS\d+(?=\s|$)"),
    "MAXUS": re.compile(r"(?:^|\s)MAXUS\d+(?=\s|$)"),
    "P99US": re.compile(r"(?:^|\s)P99US\d+(?=\s|$)"),
    "H": re.compile(r"(?:^|\s)H\d+(?:/\d+){9}(?=\s|$)"),
    "V": re.compile(r"(?:^|\s)V\d+(?=\s|$)"),
}


class AuditError(ValueError):
    """A source is not safe to use as ACCEPT timing evidence."""


@dataclass(frozen=True)
class AcceptWindow:
    source: str
    source_sha256: str
    profile: str
    run_id: str
    window: int
    stage_segment: int
    stage_window: int
    state: int
    stage: int
    frames: int
    elapsed_us: int | None
    hw_fps_x10: int | None
    avg_us: int
    max_us: int
    p99_us: int
    over_budget: int
    misses: int
    histogram: tuple[int, ...]
    line_number: int

    @property
    def average_shortfall_us(self) -> int:
        return max(self.avg_us - FRAME_DEADLINE_US, 0)

    @property
    def average_headroom_us(self) -> int:
        return max(FRAME_DEADLINE_US - self.avg_us, 0)

    @property
    def average_gap_us(self) -> int:
        return self.avg_us - FRAME_DEADLINE_US

    @property
    def p99_excess_us(self) -> int:
        return max(self.p99_us - FRAME_DEADLINE_US, 0)

    @property
    def miss_density(self) -> float:
        return self.misses / self.frames

    @property
    def over_budget_density(self) -> float:
        return self.over_budget / self.frames

    @property
    def rank_score_us(self) -> float:
        return self.miss_density * self.p99_excess_us

    @property
    def near_60_band(self) -> bool:
        return NEAR_60_MIN_AVG_US <= self.avg_us <= NEAR_60_MAX_AVG_US

    def is_target(
        self,
        minimum_us: int,
        maximum_us: int,
        *,
        policy_excluded_windows: Sequence[int] = (),
    ) -> bool:
        return (
            self.misses > 0
            and self.window not in policy_excluded_windows
            and minimum_us <= self.p99_excess_us <= maximum_us
        )


@dataclass(frozen=True)
class PerfSource:
    path: Path
    sha256: str
    aliases: tuple[Path, ...]
    profile: str
    run_id: str
    windows: tuple[AcceptWindow, ...]
    end_count: int


@dataclass(frozen=True)
class RejectedSource:
    path: Path
    reason: str


@dataclass(frozen=True)
class ScanResult:
    scanned_files: int
    ignored_without_accept: int
    sources: tuple[PerfSource, ...]
    rejected: tuple[RejectedSource, ...]
    duplicate_aliases: tuple[tuple[Path, Path], ...]


def _decimal_x10(match: re.Match[str], stem: str) -> int:
    return int(match.group(f"{stem}_whole")) * 10 + int(
        match.group(f"{stem}_tenth")
    )


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise AuditError(f"cannot read {path}: {exc}") from exc
    return digest.hexdigest().upper()


def _read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise AuditError(f"cannot read {path}: {exc}") from exc


def _parse_accept(
    body: str,
    *,
    source: str,
    source_sha256: str,
    profile: str,
    run_id: str,
    window: int,
    line_number: int,
) -> AcceptWindow:
    duplicate_or_missing = [
        name
        for name, pattern in CORE_FIELD_PATTERNS.items()
        if len(pattern.findall(body)) != 1
    ]
    if duplicate_or_missing:
        raise AuditError(
            f"line {line_number}: required ACCEPT field count is not one: "
            + ",".join(duplicate_or_missing)
        )
    match = ACCEPT_RE.fullmatch(body)
    if match is None:
        raise AuditError(f"line {line_number}: malformed PERF ACCEPT record")

    state = int(match.group("state"))
    stage = int(match.group("stage"))
    frames = int(match.group("frames"))
    elapsed_text = match.group("elapsed_us")
    elapsed_us = int(elapsed_text) if elapsed_text is not None else None
    hw_fps_x10 = (
        int(match.group("hw_whole")) * 10 + int(match.group("hw_tenth"))
        if match.group("hw_whole") is not None
        else None
    )
    avg_x10 = _decimal_x10(match, "avg")
    max_x10 = _decimal_x10(match, "max")
    p99_x10 = _decimal_x10(match, "p99")
    avg_us = int(match.group("avg_us"))
    max_us = int(match.group("max_us"))
    p99_us = int(match.group("p99_us"))
    over_budget = int(match.group("over_budget"))
    misses = int(match.group("misses"))
    histogram = tuple(int(value) for value in match.group("histogram").split("/"))
    valid = int(match.group("valid"))

    errors: list[str] = []
    if state != 2:
        errors.append(f"requires gameplay S2, got S{state}")
    if stage < 0:
        errors.append(f"negative stage ST{stage}")
    if not 1 <= frames <= 120:
        errors.append(f"N{frames} is outside 1..120")
    if valid != 1:
        errors.append(f"window latch V{valid} is not usable")
    me_faults = re.findall(r"(?:^|\s)MEFAULT(\d+)(?=\s|$)", body)
    if len(me_faults) > 1:
        errors.append("duplicate MEFAULT field")
    elif me_faults and int(me_faults[0]) != 0:
        errors.append(f"MEFAULT{me_faults[0]} is nonzero")
    if elapsed_us is not None:
        if elapsed_us <= 0:
            errors.append("ELUS must be positive")
        else:
            expected_fps_x10 = frames * 10_000_000 // elapsed_us
            if hw_fps_x10 != expected_fps_x10:
                errors.append(
                    f"HWFPS{hw_fps_x10 / 10:.1f} does not match "
                    f"N{frames}/ELUS{elapsed_us} "
                    f"(expected {expected_fps_x10 / 10:.1f})"
                )
    if len(histogram) != 10 or sum(histogram) != frames:
        errors.append(f"histogram sum {sum(histogram)} does not match N{frames}")
    if over_budget > frames:
        errors.append(f"OVR{over_budget} exceeds N{frames}")
    # Bucket 6 begins at sampleUs >= 16667 while OVR uses sampleUs > 16667.
    # Exact-budget samples can therefore make this an inequality, not equality.
    if len(histogram) == 10 and over_budget > sum(histogram[6:]):
        errors.append("OVR exceeds the >=16667us histogram population")
    if max_us < avg_us:
        errors.append(f"MAXUS{max_us} is smaller than AVGUS{avg_us}")
    if max_us < p99_us:
        errors.append(f"MAXUS{max_us} is smaller than P99US{p99_us}")
    if not 0 <= avg_us - avg_x10 * 100 <= 199:
        errors.append("AVG and AVGUS are inconsistent")
    if not 0 <= max_us - max_x10 * 100 <= 99:
        errors.append("MAX and MAXUS are inconsistent")
    if not 0 <= p99_us - p99_x10 * 100 <= 99:
        errors.append("P99 and P99US are inconsistent")

    if errors:
        raise AuditError(f"line {line_number}: " + "; ".join(errors))

    return AcceptWindow(
        source=source,
        source_sha256=source_sha256,
        profile=profile,
        run_id=run_id,
        window=window,
        stage_segment=0,
        stage_window=0,
        state=state,
        stage=stage,
        frames=frames,
        elapsed_us=elapsed_us,
        hw_fps_x10=hw_fps_x10,
        avg_us=avg_us,
        max_us=max_us,
        p99_us=p99_us,
        over_budget=over_budget,
        misses=misses,
        histogram=histogram,
        line_number=line_number,
    )


def parse_source(path: Path, *, display_path: str | None = None) -> PerfSource:
    """Parse one complete hardware log and reject ambiguous timing evidence."""

    path = Path(path)
    source_sha256 = _sha256(path)
    source_label = display_path or str(path)
    lines = _read_lines(path)
    errors: list[str] = []
    windows: list[AcceptWindow] = []
    identities: set[tuple[str, str]] = set()
    last_window = 0
    last_core_kind: str | None = None
    end_count = 0

    for line_number, raw_line in enumerate(lines, 1):
        line = raw_line.strip()
        if PERF_OVERFLOW_RE.search(line):
            errors.append(f"line {line_number}: PERF log overflow")
        if FIXED_30_RE.search(line):
            errors.append(f"line {line_number}: fixed-30 mode is not 60 Hz evidence")

        tagged = TAGGED_PERF_RE.search(line)
        if tagged is None:
            if re.search(r"\bPERF\s+PF\S+\s+RID\S+\s+W\S+\s+ACCEPT\b", line):
                errors.append(f"line {line_number}: malformed tagged PERF ACCEPT")
            continue

        profile = tagged.group("profile")
        run_id = tagged.group("run_id").upper()
        window = int(tagged.group("window"))
        body = tagged.group("body")
        identities.add((profile, run_id))

        if body.startswith("ACCEPT"):
            try:
                parsed = _parse_accept(
                    body,
                    source=source_label,
                    source_sha256=source_sha256,
                    profile=profile,
                    run_id=run_id,
                    window=window,
                    line_number=line_number,
                )
            except AuditError as exc:
                errors.append(str(exc))
                last_core_kind = "accept"
                continue
            expected_window = last_window + 1
            if window != expected_window:
                errors.append(
                    f"line {line_number}: expected contiguous W{expected_window}, got W{window}"
                )
            else:
                windows.append(parsed)
                last_window = window
            last_core_kind = "accept"
            continue

        end_match = END_RE.fullmatch(body)
        if end_match is not None:
            end_count += 1
            if int(end_match.group("valid")) != 1 or int(end_match.group("drop")) != 0:
                errors.append(f"line {line_number}: END must be VALID=1 DROP=0")
            if last_window == 0:
                errors.append(f"line {line_number}: END precedes every ACCEPT window")
            elif window != last_window:
                errors.append(
                    f"line {line_number}: END W{window} does not seal latest W{last_window}"
                )
            last_core_kind = "end"

    if not windows:
        errors.append("missing tagged PERF ACCEPT windows")
    if len(identities) != 1:
        errors.append(
            "input must contain exactly one PF/RID identity, got "
            f"{sorted(identities)}"
        )
    if end_count == 0:
        errors.append("missing PERF END marker")
    if last_core_kind != "end":
        errors.append("trailing PERF ACCEPT tail is not sealed by END")
    if windows and not any(window.frames == 120 for window in windows):
        errors.append("run has no complete N120 timing window")

    if errors:
        raise AuditError("\n".join(errors))

    stage_segment = 0
    stage_window = 0
    previous_stage: int | None = None
    indexed_windows: list[AcceptWindow] = []
    for window in windows:
        if window.stage != previous_stage:
            stage_segment += 1
            stage_window = 0
            previous_stage = window.stage
        stage_window += 1
        indexed_windows.append(
            replace(
                window,
                stage_segment=stage_segment,
                stage_window=stage_window,
            )
        )

    profile, run_id = next(iter(identities))
    return PerfSource(
        path=path,
        sha256=source_sha256,
        aliases=(),
        profile=profile,
        run_id=run_id,
        windows=tuple(indexed_windows),
        end_count=end_count,
    )


def expand_inputs(inputs: Sequence[Path]) -> tuple[Path, ...]:
    paths: set[Path] = set()
    for raw_path in inputs:
        path = Path(raw_path)
        if path.is_dir():
            paths.update(
                child
                for child in path.rglob("*")
                if child.is_file() and child.suffix.lower() == ".log"
            )
        else:
            paths.add(path)
    return tuple(sorted(paths, key=lambda item: str(item)))


def scan_sources(inputs: Sequence[Path]) -> ScanResult:
    paths = expand_inputs(inputs)
    parsed_by_sha: dict[str, PerfSource] = {}
    aliases_by_sha: dict[str, list[Path]] = {}
    rejected: list[RejectedSource] = []
    duplicate_aliases: list[tuple[Path, Path]] = []
    ignored = 0

    for path in paths:
        lines = _read_lines(path)
        if not any(
            TAGGED_PERF_RE.search(line)
            and TAGGED_PERF_RE.search(line).group("body").startswith("ACCEPT")
            for line in lines
        ):
            ignored += 1
            continue
        try:
            source = parse_source(path)
        except AuditError as exc:
            rejected.append(RejectedSource(path=path, reason=str(exc)))
            continue
        if source.sha256 in parsed_by_sha:
            canonical = parsed_by_sha[source.sha256].path
            aliases_by_sha[source.sha256].append(path)
            duplicate_aliases.append((path, canonical))
            continue
        parsed_by_sha[source.sha256] = source
        aliases_by_sha[source.sha256] = []

    sources = tuple(
        replace(source, aliases=tuple(aliases_by_sha[source.sha256]))
        for source in parsed_by_sha.values()
    )
    return ScanResult(
        scanned_files=len(paths),
        ignored_without_accept=ignored,
        sources=sources,
        rejected=tuple(rejected),
        duplicate_aliases=tuple(duplicate_aliases),
    )


def ranked_windows(source: PerfSource) -> list[AcceptWindow]:
    """Rank windows inside one run only; cross-run ranking is forbidden."""

    return sorted(
        source.windows,
        key=lambda window: (
            -window.rank_score_us,
            -window.miss_density,
            -window.p99_excess_us,
            window.window,
        ),
    )


def _source_for_reference(scan: ScanResult, reference: Path) -> PerfSource:
    reference_sha = _sha256(reference)
    for source in scan.sources:
        if source.sha256 == reference_sha:
            return source
    rejected = next(
        (item for item in scan.rejected if item.path.resolve() == reference.resolve()),
        None,
    )
    if rejected is not None:
        raise AuditError(f"reference log was rejected: {rejected.reason}")
    raise AuditError("reference log is absent from the validated input set")


def build_map(
    scan: ScanResult,
    reference: Path,
    *,
    reference_build_sha256: str,
    observer_on: Sequence[Path] = (),
    policy_excluded_windows: Sequence[int] = (),
    target_min_us: int = DEFAULT_TARGET_MIN_US,
    target_max_us: int = DEFAULT_TARGET_MAX_US,
) -> dict[str, object]:
    if target_min_us < 0 or target_max_us < target_min_us:
        raise AuditError("target shortfall range is invalid")
    if re.fullmatch(r"[0-9A-Fa-f]{64}", reference_build_sha256) is None:
        raise AuditError("reference build SHA-256 must be exactly 64 hexadecimal digits")
    reference_build_sha256 = reference_build_sha256.upper()
    reference_source = _source_for_reference(scan, reference)
    policy_excluded_set = set(policy_excluded_windows)
    invalid_exclusions = sorted(
        window
        for window in policy_excluded_set
        if window < 1 or window > len(reference_source.windows)
    )
    if invalid_exclusions:
        raise AuditError(
            "policy-excluded W is outside the reference run: "
            f"{invalid_exclusions}"
        )
    observer_shas = {_sha256(path) for path in observer_on}
    if reference_source.sha256 in observer_shas:
        raise AuditError("reference log is also classified observer-on")
    known_source_shas = {source.sha256 for source in scan.sources}
    unknown_observers = observer_shas - known_source_shas
    if unknown_observers:
        raise AuditError("an observer-on log is absent from the validated input set")

    reference_ranked = ranked_windows(reference_source)
    targets = [
        window
        for window in reference_ranked
        if window.is_target(
            target_min_us,
            target_max_us,
            policy_excluded_windows=policy_excluded_set,
        )
    ]
    below_deadline_misses = sorted(
        (
            window
            for window in reference_source.windows
            if window.misses > 0 and window.avg_us <= FRAME_DEADLINE_US
        ),
        key=lambda window: (-window.miss_density, -window.over_budget_density),
    )
    near_average_band = [
        window
        for window in reference_ranked
        if window.misses > 0
        and window.near_60_band
        and window.window not in policy_excluded_set
    ]
    policy_excluded = [
        window
        for window in reference_ranked
        if window.window in policy_excluded_set
    ]

    source_maps: list[dict[str, object]] = []
    for source in scan.sources:
        if source.sha256 == reference_source.sha256:
            role = "observer-off reference (caller asserted; build SHA fixed)"
            comparison_eligible = True
        elif source.sha256 in observer_shas:
            role = "observer-on evidence only; excluded from adoption map"
            comparison_eligible = False
        else:
            role = "unclassified historical run; not comparison-eligible"
            comparison_eligible = False
        source_maps.append(
            {
                "source": _source_dict(
                    source,
                    performance_role=role,
                    comparison_eligible=comparison_eligible,
                ),
                # This order is independent within the source.  Never flatten
                # these lists into a cross-run performance ranking.
                "ranked_windows": [
                    _window_dict(
                        window,
                        source_rank=rank,
                        policy_excluded=(
                            source.sha256 == reference_source.sha256
                            and window.window in policy_excluded_set
                        ),
                    )
                    for rank, window in enumerate(ranked_windows(source), 1)
                ],
            }
        )

    return {
        "valid": True,
        "metric_contract": {
            "deadline_us": FRAME_DEADLINE_US,
            "miss_density": "MISS / N",
            "p99_excess_us": "max(P99US - 16667, 0)",
            "average_gap_us": "AVGUS - 16667 (signed, secondary)",
            "mean_shortfall_us": "max(AVGUS - 16667, 0) (secondary)",
            "mean_headroom_us": "max(16667 - AVGUS, 0)",
            "rank_score_us": "(MISS / N) * p99_excess_us",
            "warning": (
                "P99 excess is a targeting proxy, not a required-saving proof; "
                "MAX, histogram buckets and within-bucket variance remain"
            ),
        },
        "inventory": {
            "scanned_files": scan.scanned_files,
            "ignored_without_accept": scan.ignored_without_accept,
            "validated_unique_sources": len(scan.sources),
            "validated_windows": sum(len(source.windows) for source in scan.sources),
            "rejected_sources": len(scan.rejected),
            "duplicate_aliases": len(scan.duplicate_aliases),
        },
        "reference": {
            **_source_dict(
                reference_source,
                performance_role=(
                    "observer-off reference (caller asserted; build SHA fixed)"
                ),
                comparison_eligible=True,
            ),
            "build_sha256": reference_build_sha256,
        },
        "target_range_us": [target_min_us, target_max_us],
        "target_metric": "P99 excess proxy; candidate only",
        "policy_exclusion_contract": (
            "explicit reference global W identity under fixed log/build SHA; "
            "never inferred from another run's stage-local ordinal"
        ),
        "reference_targets": [
            _window_dict(
                window,
                source_rank=reference_ranked.index(window) + 1,
                policy_excluded=False,
            )
            for window in targets
        ],
        "reference_near_average_band": [
            _window_dict(
                window,
                source_rank=reference_ranked.index(window) + 1,
                policy_excluded=False,
            )
            for window in near_average_band
        ],
        "reference_policy_excluded": [
            _window_dict(
                window,
                source_rank=reference_ranked.index(window) + 1,
                policy_excluded=True,
            )
            for window in policy_excluded
        ],
        "reference_below_deadline_misses": [
            _window_dict(
                window,
                source_rank=reference_ranked.index(window) + 1,
                policy_excluded=window.window in policy_excluded_set,
            )
            for window in below_deadline_misses
        ],
        "reference_ranked_windows": [
            _window_dict(
                window,
                source_rank=rank,
                policy_excluded=window.window in policy_excluded_set,
            )
            for rank, window in enumerate(reference_ranked, 1)
        ],
        "source_maps": source_maps,
        "rejected": [
            {"path": str(item.path), "reason": item.reason}
            for item in scan.rejected
        ],
        "duplicate_aliases": [
            {"alias": str(alias), "canonical": str(canonical)}
            for alias, canonical in scan.duplicate_aliases
        ],
    }


def _window_dict(
    window: AcceptWindow,
    *,
    source_rank: int | None = None,
    policy_excluded: bool = False,
) -> dict[str, object]:
    data = asdict(window)
    data["histogram"] = list(window.histogram)
    data.update(
        {
            "source_rank": source_rank,
            "average_gap_us": window.average_gap_us,
            "average_shortfall_us": window.average_shortfall_us,
            "average_headroom_us": window.average_headroom_us,
            "p99_excess_us": window.p99_excess_us,
            "miss_density": round(window.miss_density, 9),
            "over_budget_density": round(window.over_budget_density, 9),
            "rank_score_us": round(window.rank_score_us, 6),
            "near_60_band": window.near_60_band,
            "policy_excluded": policy_excluded,
        }
    )
    return data


def _source_dict(
    source: PerfSource,
    *,
    performance_role: str,
    comparison_eligible: bool,
) -> dict[str, object]:
    return {
        "path": str(source.path),
        "sha256": source.sha256,
        "aliases": [str(path) for path in source.aliases],
        "profile": source.profile,
        "run_id": source.run_id,
        "windows": len(source.windows),
        "frames": sum(window.frames for window in source.windows),
        "stages": sorted({window.stage for window in source.windows}),
        "misses": sum(window.misses for window in source.windows),
        "performance_role": performance_role,
        "comparison_eligible": comparison_eligible,
    }


def _short_source(path: str) -> str:
    return Path(path).name


def _histogram_text(window: dict[str, object]) -> str:
    histogram = window["histogram"]
    assert isinstance(histogram, list)
    return "/".join(str(value) for value in histogram)


def format_markdown(result: dict[str, object], *, reference_top: int = 20) -> str:
    inventory = result["inventory"]
    contract = result["metric_contract"]
    reference = result["reference"]
    targets = result["reference_targets"]
    near_average = result["reference_near_average_band"]
    policy_excluded = result["reference_policy_excluded"]
    below = result["reference_below_deadline_misses"]
    reference_ranked = result["reference_ranked_windows"]
    source_maps = result["source_maps"]
    rejected = result["rejected"]
    duplicates = result["duplicate_aliases"]
    assert isinstance(inventory, dict)
    assert isinstance(contract, dict)
    assert isinstance(reference, dict)
    assert isinstance(targets, list)
    assert isinstance(near_average, list)
    assert isinstance(policy_excluded, list)
    assert isinstance(below, list)
    assert isinstance(reference_ranked, list)
    assert isinstance(source_maps, list)
    assert isinstance(rejected, list)
    assert isinstance(duplicates, list)

    rows = [
        "# TH07 PSP MISS targeting map",
        "",
        "## Metric contract",
        "",
        f"- Deadline: `{contract['deadline_us']} us` (producer `kFrameBudgetUs`).",
        "- MISS density: `MISS / N`; MISS is the producer's VCount delta-minus-one sum.",
        "- Tail-shortfall proxy: `max(P99US - 16667, 0)`; AVG gap/headroom is retained separately.",
        "- Rank score: `MISS density * P99 excess us`.",
        "- This is a candidate-ordering proxy, not a saving requirement or a 60 Hz guarantee. "
        "MAX, histogram buckets and within-bucket variance remain.",
        "",
        "## Evidence inventory",
        "",
        f"- Scanned `{inventory['scanned_files']}` files; ignored "
        f"`{inventory['ignored_without_accept']}` without tagged ACCEPT evidence.",
        f"- Validated `{inventory['validated_unique_sources']}` unique sources / "
        f"`{inventory['validated_windows']}` windows.",
        f"- Rejected `{inventory['rejected_sources']}` sources; collapsed "
        f"`{inventory['duplicate_aliases']}` byte-identical aliases.",
        f"- Reference log: `{reference['path']}` / PF{reference['profile']} "
        f"RID{reference['run_id']} / log SHA-256 `{reference['sha256']}`.",
        f"- Recorded reference build SHA-256: `{reference['build_sha256']}`.",
        "- Only that caller-asserted observer-off reference is ranked for adoption. Other runs are "
        "audited independently and never mixed. A verdict still requires same-replay W/S/ST/N pairing.",
        "",
        f"## Reference sniper candidates (P99 excess +{result['target_range_us'][0]} to "
        f"+{result['target_range_us'][1]} us)",
        "",
        "These are estimates only; they do not mean that an equal AVG saving will necessarily clear MISS.",
        "",
    ]
    if targets:
        rows.extend(
            [
                "| map rank | ST / stage-W | global W | AVG gap | P99 excess | MISS/N | OVR/N | MAXUS | H0..H9 | score |",
                "|---:|---:|---:|---:|---:|---:|---:|---:|:---|---:|",
            ]
        )
        for window in targets:
            rows.append(
                f"| {window['source_rank']} | {window['stage']} / {window['stage_window']} | "
                f"{window['window']} | {int(window['average_gap_us']):+d} | "
                f"{window['p99_excess_us']} | "
                f"{window['misses']}/{window['frames']} "
                f"({float(window['miss_density']) * 100:.1f}%) | "
                f"{window['over_budget']}/{window['frames']} | "
                f"{window['max_us']} | `{_histogram_text(window)}` | "
                f"{float(window['rank_score_us']):.3f} |"
            )
    else:
        rows.append("No reference window falls in the requested saving band.")

    rows.extend(
        [
            "",
            "## Reference primary rank (single run only)",
            "",
            "Only explicitly supplied global W identities under this exact reference SHA are policy-excluded.",
            "",
            "| rank | ST / stage-W | W | AVGUS | P99US | P99 excess | MISS/N | OVR/N | policy exclude | score |",
            "|---:|---:|---:|---:|---:|---:|---:|---:|:---:|---:|",
        ]
    )
    for window in reference_ranked[:reference_top]:
        rows.append(
            f"| {window['source_rank']} | {window['stage']} / {window['stage_window']} | "
            f"{window['window']} | {window['avg_us']} | {window['p99_us']} | "
            f"{window['p99_excess_us']} | "
            f"{window['misses']}/{window['frames']} "
            f"({float(window['miss_density']) * 100:.1f}%) | "
            f"{window['over_budget']}/{window['frames']} | "
            f"{'yes' if window['policy_excluded'] else 'no'} | "
            f"{float(window['rank_score_us']):.3f} |"
        )

    rows.extend(
        [
            "",
            "## AVG 16.7-17.5 ms band audit",
            "",
            "AVG alone does not establish a +0.3-0.8 ms route to MISS=0. The P99 tail is shown explicitly.",
            "",
            "| rank | ST / stage-W | W | AVGUS | P99US | P99 excess | MISS/N | H0..H9 |",
            "|---:|---:|---:|---:|---:|---:|---:|:---|",
        ]
    )
    for window in near_average:
        rows.append(
            f"| {window['source_rank']} | {window['stage']} / {window['stage_window']} | "
            f"{window['window']} | {window['avg_us']} | {window['p99_us']} | "
            f"{window['p99_excess_us']} | {window['misses']}/{window['frames']} | "
            f"`{_histogram_text(window)}` |"
        )
    if not near_average:
        rows.append("| - | - | - | - | - | - | - | - |")

    rows.extend(
        [
            "",
            "## MISS with AVG headroom (variance queue)",
            "",
            "These have `AVGUS <= 16667`; the tail can still miss even though the signed AVG gap is non-positive.",
            "",
            "| rank | ST / stage-W | W | AVG gap | P99 excess | MISS/N | OVR/N | MAXUS |",
            "|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for window in below[:20]:
        rows.append(
            f"| {window['source_rank']} | {window['stage']} / {window['stage_window']} | "
            f"{window['window']} | {int(window['average_gap_us']):+d} | "
            f"{window['p99_excess_us']} | {window['misses']}/{window['frames']} | "
            f"{window['over_budget']}/{window['frames']} | {window['max_us']} |"
        )
    if not below:
        rows.append("| - | - | - | - | - | - | - | - |")

    rows.extend(["", "## Per-source evidence roles (never cross-ranked)", ""])
    rows.extend(
        [
            "| source | PF/RID | log SHA-256 | windows | comparison role |",
            "|:---|:---|:---|---:|:---|",
        ]
    )
    for source_map in source_maps:
        source = source_map["source"]
        assert isinstance(source, dict)
        rows.append(
            f"| `{_short_source(str(source['path']))}` | "
            f"PF{source['profile']}/RID{source['run_id']} | "
            f"`{str(source['sha256'])[:16]}...` | {source['windows']} | "
            f"{source['performance_role']} |"
        )

    if policy_excluded:
        rows.extend(
            [
                "",
                "Policy exclusions apply only to the displayed reference global-W identities. "
                "A stage-local ordinal from another run is never inferred to be equivalent.",
            ]
        )

    rows.extend(["", "## Rejected/duplicate input audit", ""])
    if rejected:
        rows.extend(["| source | reason |", "|:---|:---|"])
        for item in rejected:
            first_reason = str(item["reason"]).splitlines()[0]
            rows.append(f"| `{_short_source(str(item['path']))}` | {first_reason} |")
    else:
        rows.append("No rejected ACCEPT source.")
    if duplicates:
        rows.extend(["", "Byte-identical aliases:"])
        for item in duplicates:
            rows.append(
                f"- `{_short_source(str(item['alias']))}` -> "
                f"`{_short_source(str(item['canonical']))}`"
            )

    rows.extend(
        [
            "",
            "The JSON/CSV modes contain every validated window ranked independently inside its own run.",
        ]
    )
    return "\n".join(rows)


CSV_FIELDS = (
    "source_rank",
    "source",
    "source_sha256",
    "profile",
    "run_id",
    "window",
    "stage_segment",
    "stage_window",
    "stage",
    "frames",
    "avg_us",
    "average_gap_us",
    "average_shortfall_us",
    "average_headroom_us",
    "p99_excess_us",
    "misses",
    "miss_density",
    "over_budget",
    "over_budget_density",
    "p99_us",
    "max_us",
    "rank_score_us",
    "near_60_band",
    "policy_excluded",
    "histogram",
    "performance_role",
    "comparison_eligible",
)


def format_csv(result: dict[str, object]) -> str:
    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=CSV_FIELDS, extrasaction="ignore")
    writer.writeheader()
    source_maps = result["source_maps"]
    assert isinstance(source_maps, list)
    for source_map in source_maps:
        source = source_map["source"]
        windows = source_map["ranked_windows"]
        assert isinstance(source, dict)
        assert isinstance(windows, list)
        for window in windows:
            row = dict(window)
            row["histogram"] = "/".join(str(value) for value in row["histogram"])
            row["performance_role"] = source["performance_role"]
            row["comparison_eligible"] = source["comparison_eligible"]
            writer.writerow(row)
    return output.getvalue().rstrip("\r\n")


def _parse_window_spec(value: str) -> tuple[int, ...]:
    match = re.fullmatch(r"(\d+)(?:-(\d+))?", value)
    if match is None:
        raise argparse.ArgumentTypeError("window must be W number or inclusive A-B range")
    first = int(match.group(1))
    last = int(match.group(2)) if match.group(2) is not None else first
    if first < 1 or last < first:
        raise argparse.ArgumentTypeError("window range must satisfy 1 <= first <= last")
    return tuple(range(first, last + 1))


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Rank fail-closed PSP PERF ACCEPT windows for MISS hunting."
    )
    parser.add_argument(
        "inputs",
        nargs="+",
        type=Path,
        help="hardware log file(s), or directories whose .LOG/.log files are scanned recursively",
    )
    parser.add_argument(
        "--reference",
        required=True,
        type=Path,
        help="explicit observer-off reference log used for the sniper list",
    )
    parser.add_argument(
        "--reference-build-sha256",
        required=True,
        help="fixed EBOOT/PBP SHA-256 recorded for the observer-off reference",
    )
    parser.add_argument(
        "--observer-on",
        action="append",
        default=[],
        type=Path,
        help="validated observer-on log to mark evidence-only (repeatable)",
    )
    parser.add_argument(
        "--exclude-window",
        action="append",
        default=[],
        type=_parse_window_spec,
        help=(
            "explicit reference global W or inclusive range (for example 12-15); "
            "repeatable and never transferred across runs"
        ),
    )
    parser.add_argument(
        "--target-min-us", type=int, default=DEFAULT_TARGET_MIN_US
    )
    parser.add_argument(
        "--target-max-us", type=int, default=DEFAULT_TARGET_MAX_US
    )
    parser.add_argument(
        "--format", choices=("markdown", "json", "csv"), default="markdown"
    )
    parser.add_argument(
        "--reference-top",
        type=int,
        default=20,
        help="maximum rows in the Markdown reference-run primary ranking",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)
    try:
        scan = scan_sources(args.inputs)
        result = build_map(
            scan,
            args.reference,
            reference_build_sha256=args.reference_build_sha256,
            observer_on=args.observer_on,
            policy_excluded_windows=tuple(
                window
                for window_group in args.exclude_window
                for window in window_group
            ),
            target_min_us=args.target_min_us,
            target_max_us=args.target_max_us,
        )
    except AuditError as exc:
        print(f"MISS MAP INVALID: {exc}", file=sys.stderr)
        return 2

    if args.format == "json":
        print(json.dumps(result, indent=2, sort_keys=True))
    elif args.format == "csv":
        print(format_csv(result))
    else:
        print(format_markdown(result, reference_top=args.reference_top))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

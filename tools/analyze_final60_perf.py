#!/usr/bin/env python3
"""Validate TH07 PSP Final60 profiler logs without trusting stale/partial runs."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Iterable


PF_RE = re.compile(r"(?:^|\s)PF([A-Z0-9_]+)(?=\s|$)")
RID_RE = re.compile(r"(?:^|\s)RID([0-9A-Fa-f]{8})(?=\s|$)")
WINDOW_RE = re.compile(r"(?:^|\s)W(\d+)(?=\s|$)")
SUMMARY_RE = re.compile(r"(?:^|\s)S-?\d+(?=\s|$)")
ACCEPT_HISTOGRAM_RE = re.compile(r"(?:^|\s)H(\d+(?:/\d+){9})(?=\s|$)")
SEPARATE_HISTOGRAM_RE = re.compile(r"(?:^|\s)H([0-9])(\d+)(?=\s|$)")

ACCEPT_PROFILES = {"ACCEPT", "EMPTY_M2", "EMPTY_M3"}
M3_SAMPLE_STRIDE = 32
A1_SAME_LABELS = ("RAB", "DSP", "RAD", "RAE", "BUP")
A1_SAME_REASON_MASKS = {
    "RAB": 0x00000F52,
    "DSP": 0x0000000C,
    "RAD": 0x00000080,
    "RAE": 0x0000022C,
    "BUP": 0x00001000,
}
A1_SAME_MODE_MASKS = {
    "RAB": 0x0000000B,
    "DSP": 0x00000001,
    "RAD": 0x00000001,
    "RAE": 0x0000000B,
    "BUP": 0x00000001,
}


def _unique_numeric_values(
    line: str,
    names: tuple[str, ...],
    line_number: int,
    label: str,
    errors: list[str],
) -> dict[str, str]:
    values: dict[str, str] = {}
    for name in names:
        matches = re.findall(
            rf"(?:^|\s){re.escape(name)}([-+]?\d+(?:\.\d+)?)(?=\s|$)", line
        )
        if not matches:
            errors.append(f"line {line_number}: {label} missing {name}")
        elif len(matches) > 1:
            errors.append(f"line {line_number}: {label} duplicate {name}")
        else:
            values[name] = matches[0]
    return values


def _metadata(line: str) -> tuple[str, str, int] | None:
    profiles = PF_RE.findall(line)
    run_ids = RID_RE.findall(line)
    windows = WINDOW_RE.findall(line)
    if len(profiles) != 1 or len(run_ids) != 1 or len(windows) != 1:
        return None
    return profiles[0], run_ids[0].upper(), int(windows[0])


def _record_kind(line: str) -> str:
    words = set(re.findall(r"\b[A-Z][A-Z0-9_]*\b", line))
    if "END" in words:
        return "end"
    if "OVERFLOW" in words:
        return "overflow"
    if "A1S" in words:
        return "a1s"
    if "ACCEPT" in words:
        return "accept"
    if "M3S" in words:
        return "m3s"
    if "M3" in words:
        return "m3"
    if "M2I" in words:
        return "m2i"
    if "APB" in words:
        return "apb"
    if "OWNMAP" in words:
        return "ownmap"
    if "OWN" in words:
        return "own"
    if "DRAW" in words:
        return "draw"
    if "HIST" in words:
        return "hist"
    if "GPU" in words:
        return "gpu"
    if SUMMARY_RE.search(line):
        return "summary"
    return "unknown"


def _exact_count(
    records: list[dict[str, object]], kind: str, expected: int, label: str,
    errors: list[str],
) -> None:
    count = sum(record["kind"] == kind for record in records)
    if count != expected:
        errors.append(f"{label}: expected {expected} {kind} line(s), found {count}")


def _histogram_from_separate(line: str) -> list[int] | None:
    matches = SEPARATE_HISTOGRAM_RE.findall(line)
    if len(matches) != 10:
        return None
    buckets: dict[int, int] = {}
    for raw_index, raw_count in matches:
        index = int(raw_index)
        if index in buckets:
            return None
        buckets[index] = int(raw_count)
    if set(buckets) != set(range(10)):
        return None
    return [buckets[index] for index in range(10)]


def _histogram_from_accept(line: str) -> list[int] | None:
    matches = ACCEPT_HISTOGRAM_RE.findall(line)
    if len(matches) != 1:
        return None
    return [int(value) for value in matches[0].split("/")]


def _gameplay_stage_sequence(samples: list[dict[str, object]]) -> list[int]:
    sequence: list[int] = []
    for sample in samples:
        if sample["state"] != 2:
            continue
        stage = int(sample["stage"])
        if not sequence or sequence[-1] != stage:
            sequence.append(stage)
    return sequence


def _raw_values(
    line: str, names: tuple[str, ...], line_number: int, label: str,
    errors: list[str],
) -> dict[str, int]:
    values: dict[str, int] = {}
    for name in names:
        matches = re.findall(rf"(?:^|\s){name}(\d+)(?=\s|$)", line)
        if not matches:
            errors.append(f"line {line_number}: {label} missing {name}")
        elif len(matches) > 1:
            errors.append(f"line {line_number}: {label} duplicate {name}")
        else:
            values[name] = int(matches[0])
    return values


def _validate_draw_line(line: str, line_number: int, errors: list[str]) -> None:
    priorities = [
        int(priority)
        for priority in re.findall(
            r"(?:^|\s)P(\d{2})[A-Z]+\d+\.\d+(?=\s|$)", line
        )
    ]
    if sorted(priorities) != list(range(18)):
        errors.append(
            f"line {line_number}: DRAW priorities must contain P00..P17 exactly once"
        )
    parsed = _unique_numeric_values(
        line,
        ("SUM", "R", "OH", "ERR", "LIM", "OOR", "OE", "OWNOV", "OWNTR", "G"),
        line_number,
        "DRAW",
        errors,
    )
    if parsed.get("G") != "1":
        errors.append(f"line {line_number}: M2 closure failed")
    if "ERR" in parsed and "LIM" in parsed and int(float(parsed["ERR"])) > int(float(parsed["LIM"])):
        errors.append(f"line {line_number}: DRAW ERR exceeds LIM")
    for name, label in (
        ("OOR", "draw priority out of range"),
        ("OE", "owner closure mismatch"),
        ("OWNOV", "owner table overflow"),
        ("OWNTR", "owner log truncation"),
    ):
        if parsed.get(name) not in (None, "0"):
            errors.append(f"line {line_number}: {label}")


def _validate_m3_line(
    line: str, line_number: int, frames: int | None, errors: list[str]
) -> None:
    values = _raw_values(
        line,
        (
            "BUUS", "LZUS", "ITUS", "BTUS", "SUMUS", "ERR", "IERR", "LIM",
            "VIS", "OOR", "G",
        ),
        line_number,
        "M3",
        errors,
    )
    population_tenths: dict[str, int] = {}
    for name in ("NL", "NI", "NB"):
        matches = re.findall(
            rf"(?:^|\s){name}(\d+\.\d)(?=\s|$)", line
        )
        if not matches:
            errors.append(f"line {line_number}: M3 missing {name}")
        elif len(matches) > 1:
            errors.append(f"line {line_number}: M3 duplicate {name}")
        else:
            whole, fraction = matches[0].split(".")
            population_tenths[name] = int(whole) * 10 + int(fraction)
    frame_matches = re.findall(r"(?:^|\s)F(\d+)/(\d+)(?=\s|$)", line)
    if not frame_matches:
        errors.append(f"line {line_number}: M3 missing frame closure")
    elif len(frame_matches) > 1:
        errors.append(f"line {line_number}: M3 duplicate F")
    elif frames is not None and (
        int(frame_matches[0][0]) != frames or int(frame_matches[0][1]) != frames
    ):
        errors.append(
            f"line {line_number}: M3 frame closure F{frame_matches[0][0]}/{frame_matches[0][1]} != N{frames}"
        )
    if frames is not None and "VIS" in values and "NB" in population_tenths:
        expected_bullet_tenths = values["VIS"] * 10 // frames
        if population_tenths["NB"] != expected_bullet_tenths:
            errors.append(
                f"line {line_number}: M3 NB{population_tenths['NB'] / 10:.1f} "
                f"!= VIS{values['VIS']}/N{frames} ({expected_bullet_tenths / 10:.1f})"
            )
    if all(name in values for name in ("LZUS", "ITUS", "BTUS", "SUMUS")):
        phase_sum = values["LZUS"] + values["ITUS"] + values["BTUS"]
        if phase_sum != values["SUMUS"]:
            errors.append(
                f"line {line_number}: M3 raw phase sum {phase_sum} != SUMUS{values['SUMUS']}"
            )
    if all(name in values for name in ("BUUS", "SUMUS", "ERR")):
        expected_error = abs(values["BUUS"] - values["SUMUS"])
        if values["ERR"] != expected_error:
            errors.append(
                f"line {line_number}: M3 ERR{values['ERR']} != raw closure {expected_error}"
            )
    if frames is not None and all(name in values for name in ("BUUS", "LIM")):
        expected_limit = max(200 * frames, values["BUUS"] // 50)
        if values["LIM"] != expected_limit:
            errors.append(
                f"line {line_number}: M3 LIM{values['LIM']} != derived {expected_limit}"
            )
    if all(name in values for name in ("ERR", "IERR", "LIM")) and (
        values["ERR"] > values["LIM"] or values["IERR"] > values["LIM"]
    ):
        errors.append(f"line {line_number}: M3 ERR/IERR exceeds LIM")
    if values.get("G") != 1:
        errors.append(f"line {line_number}: M3 closure failed")
    if values.get("OOR") not in (None, 0):
        errors.append(f"line {line_number}: M3 draw priority out of range")


def _validate_m3s_line(
    line: str, line_number: int, frames: int | None, errors: list[str]
) -> None:
    values = _raw_values(
        line,
        (
            "RAWUS", "BTXUS", "LKUS", "VMUS", "VDUS", "CRUS", "STUS", "VSUS",
            "RPUS", "DCUS", "SUMUS", "ERR", "LIM", "CIUS", "CIDCUS", "COUS",
            "CODCUS", "CINB", "COUTB", "CULL", "BC", "DCN", "EXUS", "MM",
            "TMRQ8", "FRAWUS", "POVUS", "MIX", "UNRES", "G",
        ),
        line_number,
        "M3S",
        errors,
    )
    pending_matches = re.findall(r"(?:^|\s)PEND(\d+)K(?=\s|$)", line)
    if not pending_matches:
        errors.append(f"line {line_number}: M3S missing PEND")
    elif len(pending_matches) > 1:
        errors.append(f"line {line_number}: M3S duplicate PEND")
    sample_matches = re.findall(r"(?:^|\s)SAMP(\d+)/(\d+)/(\d+)(?=\s|$)", line)
    if not sample_matches:
        errors.append(f"line {line_number}: M3S missing SAMP")
    elif len(sample_matches) > 1:
        errors.append(f"line {line_number}: M3S duplicate SAMP")
    else:
        sampled_draws, samples, emitter_calls = map(int, sample_matches[0])
        minimum_samples = emitter_calls // M3_SAMPLE_STRIDE
        maximum_samples = (emitter_calls + M3_SAMPLE_STRIDE - 1) // M3_SAMPLE_STRIDE
        samples_valid = (
            sampled_draws == samples
            and minimum_samples <= samples <= maximum_samples
            and ("CULL" not in values or values["CULL"] <= samples)
        )
        if not samples_valid:
            errors.append(f"line {line_number}: M3S invalid SAMP population")
    components = ("LKUS", "VMUS", "VDUS", "CRUS", "STUS", "VSUS", "RPUS", "DCUS")
    if all(name in values for name in (*components, "SUMUS")):
        detail_sum = sum(values[name] for name in components)
        if detail_sum != values["SUMUS"]:
            errors.append(
                f"line {line_number}: M3S raw phase sum {detail_sum} != SUMUS{values['SUMUS']}"
            )
    if all(name in values for name in ("BTXUS", "SUMUS", "ERR")):
        expected_error = abs(values["BTXUS"] - values["SUMUS"])
        if values["ERR"] != expected_error:
            errors.append(
                f"line {line_number}: M3S ERR{values['ERR']} != raw closure {expected_error}"
            )
    if frames is not None and all(name in values for name in ("BTXUS", "LIM")):
        expected_limit = max(200 * frames, values["BTXUS"] // 50)
        if values["LIM"] != expected_limit:
            errors.append(
                f"line {line_number}: M3S LIM{values['LIM']} != derived {expected_limit}"
            )
    if all(name in values for name in ("ERR", "LIM")) and values["ERR"] > values["LIM"]:
        errors.append(f"line {line_number}: M3S ERR exceeds LIM")
    if all(name in values for name in ("RAWUS", "CIUS", "COUS", "BTXUS")):
        if values["CIUS"] > values["RAWUS"]:
            errors.append(f"line {line_number}: M3S CIUS exceeds RAWUS")
        else:
            expected_btx = values["RAWUS"] - values["CIUS"] + values["COUS"]
            if values["BTXUS"] != expected_btx:
                errors.append(
                    f"line {line_number}: M3S BTXUS{values['BTXUS']} != carry-adjusted {expected_btx}"
                )
    if all(name in values for name in ("CIUS", "CIDCUS")) and values["CIDCUS"] > values["CIUS"]:
        errors.append(f"line {line_number}: M3S CIDCUS exceeds CIUS")
    if all(name in values for name in ("COUS", "CODCUS")) and values["CODCUS"] > values["COUS"]:
        errors.append(f"line {line_number}: M3S CODCUS exceeds COUS")
    record_matches = re.findall(
        r"(?:^|\s)REC(\d+)/(\d+)/(\d+)/(\d+)(?=\s|$)", line
    )
    if not record_matches:
        errors.append(f"line {line_number}: M3S missing REC")
    elif len(record_matches) > 1:
        errors.append(f"line {line_number}: M3S duplicate REC")
    elif sample_matches and all(
        name in values for name in ("TMRQ8", "FRAWUS", "POVUS")
    ):
        _, samples, emitter_calls = map(int, sample_matches[0])
        phase_records = sum(map(int, record_matches[0]))
        if samples and phase_records < samples:
            errors.append(
                f"line {line_number}: M3S phase records {phase_records} < samples {samples}"
            )
        expected_probe_us = (
            values["TMRQ8"] * (phase_records + 2 * samples) * emitter_calls
            + 128 * samples
        ) // (256 * samples) if samples else 0
        if values["POVUS"] != expected_probe_us:
            errors.append(
                f"line {line_number}: M3S POVUS{values['POVUS']} "
                f"!= timer-derived {expected_probe_us}"
            )
        corrected_frontend = sum(
            values[name] for name in ("VMUS", "VDUS", "CRUS", "STUS", "VSUS")
        )
        if values["FRAWUS"] < values["POVUS"]:
            errors.append(f"line {line_number}: M3S probe overhead exceeds raw frontend")
        elif corrected_frontend != values["FRAWUS"] - values["POVUS"]:
            errors.append(
                f"line {line_number}: M3S corrected frontend {corrected_frontend} "
                f"!= FRAWUS-POVUS {values['FRAWUS'] - values['POVUS']}"
            )
        if samples and not 0 < values["TMRQ8"] <= 4096:
            errors.append(f"line {line_number}: M3S implausible timer calibration")
    if values.get("MM") not in (None, 0):
        errors.append(f"line {line_number}: M3S phase mismatch")
    if values.get("MIX") not in (None, 0):
        errors.append(f"line {line_number}: M3S mixed carry attribution")
    if values.get("UNRES") not in (None, 0):
        errors.append(f"line {line_number}: M3S unresolved carry attribution")
    if values.get("G") != 1:
        errors.append(f"line {line_number}: M3S closure failed")


def _validate_m2i_line(
    line: str,
    line_number: int,
    frames: int | None,
    run_id: str | None,
    owner_maps: dict[tuple[str, int], tuple[int, str]],
    errors: list[str],
) -> None:
    values = _raw_values(
        line,
        (
            "TOTUS", "PKUS", "MXUS", "STUS", "FLUS", "DCUS", "OTUS", "SUMUS",
            "ERR", "LIM", "MM", "G",
        ),
        line_number,
        "M2I",
        errors,
    )
    owner_matches = re.findall(r"(?:^|\s)I(\d+)(?=\s|$)", line)
    priority_matches = re.findall(r"(?:^|\s)P(-?\d+)(?=\s|$)", line)
    if not owner_matches:
        errors.append(f"line {line_number}: M2I missing owner index")
    elif len(owner_matches) > 1:
        errors.append(f"line {line_number}: M2I duplicate owner index")
    elif run_id is not None:
        owner_id = int(owner_matches[0])
        if (run_id, owner_id) not in owner_maps:
            errors.append(
                f"line {line_number}: M2I I{owner_id} has no OWNMAP in RID{run_id}"
            )
        elif len(priority_matches) == 1 and owner_maps[(run_id, owner_id)][0] != int(priority_matches[0]):
            errors.append(
                f"line {line_number}: M2I P{priority_matches[0]} disagrees with OWNMAP"
            )
    if not priority_matches:
        errors.append(f"line {line_number}: M2I missing priority")
    elif len(priority_matches) > 1:
        errors.append(f"line {line_number}: M2I duplicate priority")
    if all(name in values for name in ("PKUS", "MXUS", "STUS", "FLUS", "DCUS", "OTUS", "SUMUS")):
        phase_sum = sum(
            values[name] for name in ("PKUS", "MXUS", "STUS", "FLUS", "DCUS", "OTUS")
        )
        if phase_sum != values["SUMUS"]:
            errors.append(
                f"line {line_number}: M2I raw phase sum {phase_sum} != SUMUS{values['SUMUS']}"
            )
    if all(name in values for name in ("TOTUS", "SUMUS", "ERR")):
        expected_error = abs(values["TOTUS"] - values["SUMUS"])
        if values["ERR"] != expected_error:
            errors.append(
                f"line {line_number}: M2I ERR{values['ERR']} != raw closure {expected_error}"
            )
    if frames is not None and all(name in values for name in ("TOTUS", "LIM")):
        expected_limit = max(200 * frames, values["TOTUS"] // 50)
        if values["LIM"] != expected_limit:
            errors.append(
                f"line {line_number}: M2I LIM{values['LIM']} != derived {expected_limit}"
            )
    if all(name in values for name in ("ERR", "LIM")) and values["ERR"] > values["LIM"]:
        errors.append(f"line {line_number}: M2I ERR exceeds LIM")
    if values.get("MM") not in (None, 0):
        errors.append(f"line {line_number}: M2I internal attribution mismatch")
    if values.get("G") != 1:
        errors.append(f"line {line_number}: M2I closure failed")


def _validate_apb_line(
    line: str, line_number: int, frames: int | None, errors: list[str]
) -> None:
    values = _raw_values(line, ("CALL", "DIG", "FB"), line_number, "APB", errors)
    if all(name in values for name in ("CALL", "DIG", "FB")) and not any(values.values()):
        errors.append(f"line {line_number}: APB counters are all zero")
    if all(name in values for name in ("CALL", "DIG")) and (
        values["CALL"] == 0 and values["DIG"] != 0
    ):
        errors.append(f"line {line_number}: APB DIG is nonzero without CALL")
    if all(name in values for name in ("CALL", "DIG")) and values["DIG"] < values["CALL"]:
        errors.append(f"line {line_number}: APB DIG is smaller than CALL")
    if frames is not None and all(name in values for name in ("CALL", "FB")):
        if values["CALL"] + values["FB"] > frames:
            errors.append(f"line {line_number}: APB attempts exceed N{frames}")


def _validate_a1s_line(
    line: str, line_number: int, errors: list[str]
) -> dict[str, object] | None:
    tuple_pattern = (
        r"(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/"
        r"([0-9A-Fa-f]{8})/([0-9A-Fa-f]{8})"
    )
    mask_matches = re.findall(r"(?:^|\s)K([0-9A-Fa-f]{2})(?=\s|$)", line)
    active_mask: int | None = None
    if not mask_matches:
        errors.append(f"line {line_number}: A1S missing active-kind mask K")
    elif len(mask_matches) > 1:
        errors.append(f"line {line_number}: A1S duplicate active-kind mask K")
    else:
        active_mask = int(mask_matches[0], 16)
        if active_mask & ~((1 << len(A1_SAME_LABELS)) - 1):
            errors.append(f"line {line_number}: A1S active-kind mask is out of range")

    parsed: dict[str, dict[str, int]] = {}
    field_names = (
        "calls", "elapsed_us", "eligible", "affected", "item_attempts",
        "popups", "auxiliary", "reasons", "modes",
    )
    for kind, label in enumerate(A1_SAME_LABELS):
        matches = re.findall(
            rf"(?:^|\s){label}{tuple_pattern}(?=\s|$)", line
        )
        active = active_mask is not None and (active_mask & (1 << kind)) != 0
        if not matches and active:
            errors.append(f"line {line_number}: A1S K marks missing {label}")
            continue
        if len(matches) > 1:
            errors.append(f"line {line_number}: A1S duplicate {label}")
            continue
        if not matches:
            parsed[label] = {name: 0 for name in field_names}
            continue
        if not active:
            errors.append(f"line {line_number}: A1S unmasked {label} tuple")
        raw_values = matches[0]
        values = {
            name: int(raw, 16) if name in {"reasons", "modes"} else int(raw)
            for name, raw in zip(field_names, raw_values)
        }
        parsed[label] = values

    status = _raw_values(line, ("G", "O"), line_number, "A1S", errors)
    if status.get("G") != 1:
        errors.append(f"line {line_number}: A1S observer integrity failed")
    if status.get("O") != 0:
        errors.append(f"line {line_number}: A1S counter overflow")

    for kind, label in enumerate(A1_SAME_LABELS):
        if label not in parsed:
            continue
        values = parsed[label]
        active = active_mask is not None and (active_mask & (1 << kind)) != 0
        calls = values["calls"]
        payload = sum(
            values[name]
            for name in (
                "elapsed_us", "eligible", "affected", "item_attempts",
                "popups", "auxiliary", "reasons", "modes",
            )
        )
        if calls == 0 and payload != 0:
            errors.append(
                f"line {line_number}: A1S {label} has payload without calls"
            )
        if calls != 0:
            if values["reasons"] == 0:
                errors.append(f"line {line_number}: A1S {label} has no reason")
            if values["modes"] == 0:
                errors.append(f"line {line_number}: A1S {label} has no mode")
        elif active:
            errors.append(f"line {line_number}: A1S K marks zero-call {label}")
        unexpected_reasons = values["reasons"] & ~A1_SAME_REASON_MASKS[label]
        if unexpected_reasons:
            errors.append(
                f"line {line_number}: A1S {label} unexpected reason bits "
                f"0x{unexpected_reasons:08X}"
            )
        unexpected_modes = values["modes"] & ~A1_SAME_MODE_MASKS[label]
        if unexpected_modes:
            errors.append(
                f"line {line_number}: A1S {label} unexpected mode bits "
                f"0x{unexpected_modes:08X}"
            )
        if values["affected"] > values["eligible"]:
            errors.append(
                f"line {line_number}: A1S {label} affected exceeds eligible"
            )

    if parsed and not any(values["calls"] for values in parsed.values()):
        errors.append(f"line {line_number}: sparse A1S line has no event calls")
    if "RAD" in parsed and (
        parsed["RAD"]["affected"] != parsed["RAD"]["item_attempts"]
        or parsed["RAD"]["popups"] != 0
        or parsed["RAD"]["auxiliary"] != 0
    ):
        errors.append(f"line {line_number}: A1S RAD side-effect closure failed")
    if "RAE" in parsed and (
        parsed["RAE"]["item_attempts"] != parsed["RAE"]["popups"]
    ):
        errors.append(f"line {line_number}: A1S RAE item/popup closure failed")
    if "BUP" in parsed and (
        parsed["BUP"]["affected"] != parsed["BUP"]["item_attempts"]
        or parsed["BUP"]["popups"] != 0
        or (
            parsed["BUP"]["calls"] != 0
            and parsed["BUP"]["auxiliary"] < parsed["BUP"]["calls"]
        )
    ):
        errors.append(f"line {line_number}: A1S BUP bomb closure failed")
    if "DSP" in parsed and parsed["DSP"]["popups"] > parsed["DSP"]["affected"]:
        errors.append(f"line {line_number}: A1S DSP popups exceed affected bullets")

    if (
        len(parsed) != len(A1_SAME_LABELS)
        or active_mask is None
        or "G" not in status
        or "O" not in status
    ):
        return None
    return {
        "line_number": line_number,
        "active_mask": active_mask,
        "tuples": parsed,
        **status,
    }


def analyze(
    lines: Iterable[str], expected: str, require_end: bool = True,
    enforce_performance: bool | None = None,
) -> dict[str, object]:
    if enforce_performance is None:
        enforce_performance = expected == "accept"
    result: dict[str, object] = {
        "expected": expected,
        "m2_windows": 0,
        "m3_windows": 0,
        "m3_sample_windows": 0,
        "accept_windows": 0,
        "a1_same_windows": 0,
        "a1_same_samples": [],
        "accept_eligible_windows": 0,
        "accept_stage_sequence": [],
        "accept_samples": [],
        "end_markers": 0,
        "latest_profile": None,
        "latest_run_id": None,
        "latest_end_window": None,
        "errors": [],
    }
    errors: list[str] = result["errors"]  # type: ignore[assignment]
    segments: list[dict[str, object]] = []
    pending_records: list[dict[str, object]] = []
    last_perf_record_was_end = False
    saw_perf_record = False

    for line_number, raw in enumerate(lines, 1):
        line = raw.strip()
        if not re.search(r"\bPERF\b", line):
            continue
        saw_perf_record = True
        kind = _record_kind(line)
        metadata = _metadata(line)
        last_perf_record_was_end = kind == "end"
        marker = {
            "end": "END", "accept": "ACCEPT", "m3": "M3", "m3s": "M3S",
            "m2i": "M2I", "apb": "APB", "draw": "DRAW", "hist": "HIST",
            "ownmap": "OWNMAP", "a1s": "A1S",
        }.get(kind)
        if marker is not None and len(
            re.findall(rf"(?:^|\s){marker}(?=\s|$)", line)
        ) != 1:
            errors.append(f"line {line_number}: duplicate {marker} marker")
        if kind == "overflow":
            errors.append(f"line {line_number}: RAM log overflow")
        if metadata is None:
            errors.append(f"line {line_number}: missing or duplicate PF/RID/W identity")
        record: dict[str, object] = {
            "line": line,
            "line_number": line_number,
            "kind": kind,
            "metadata": metadata,
        }
        if kind != "end":
            pending_records.append(record)
            continue

        result["end_markers"] = int(result["end_markers"]) + 1
        valid_values = re.findall(r"(?:^|\s)VALID=(\d+)(?=\s|$)", line)
        drop_values = re.findall(r"(?:^|\s)DROP=(\d+)(?=\s|$)", line)
        if not valid_values or not drop_values:
            errors.append(f"line {line_number}: malformed PERF END marker")
        if len(valid_values) > 1:
            errors.append(f"line {line_number}: END duplicate VALID")
        if len(drop_values) > 1:
            errors.append(f"line {line_number}: END duplicate DROP")
        if len(valid_values) == 1 and len(drop_values) == 1 and (
            valid_values[0] != "1" or drop_values[0] != "0"
        ):
            errors.append(f"line {line_number}: invalid END marker")

        # Game deletion and process shutdown can commit the same buffer twice.
        if not pending_records and segments:
            previous_end = segments[-1]["end"]
            assert isinstance(previous_end, dict)
            if metadata is not None and metadata == previous_end["metadata"]:
                continue
        segments.append({"records": pending_records, "end": record})
        pending_records = []

    if require_end and not result["end_markers"]:
        errors.append("missing PERF END marker (partial or unflushed run)")
    elif require_end and saw_perf_record and not last_perf_record_was_end:
        errors.append("trailing partial PERF segment after the last END marker")
    if pending_records and not require_end:
        identities = [
            record["metadata"] for record in pending_records
            if record["metadata"] is not None
        ]
        synthetic_metadata = None
        if identities:
            profiles = {identity[0] for identity in identities}
            run_ids = {identity[1] for identity in identities}
            if len(profiles) == 1 and len(run_ids) == 1:
                synthetic_metadata = (
                    identities[0][0], identities[0][1],
                    max(identity[2] for identity in identities),
                )
        segments.append(
            {
                "records": pending_records,
                "end": {"metadata": synthetic_metadata, "synthetic": True},
            }
        )

    owner_maps: dict[tuple[str, int], tuple[int, str]] = {}
    run_ids_with_owner_maps: set[str] = set()
    segment_summaries: list[dict[str, object]] = []
    last_end_window_by_identity: dict[tuple[str, str], int] = {}

    for segment_index, segment in enumerate(segments, 1):
        records = segment["records"]
        assert isinstance(records, list)
        end_record = segment["end"]
        end_metadata = end_record["metadata"] if isinstance(end_record, dict) else None
        if end_metadata is None:
            errors.append(f"run {segment_index}: END identity is missing")
        profile = end_metadata[0] if end_metadata else None
        run_id = end_metadata[1] if end_metadata else None
        end_window = end_metadata[2] if end_metadata else None
        windows: dict[int, list[dict[str, object]]] = defaultdict(list)
        window_order: list[int] = []

        for record in records:
            metadata = record["metadata"]
            if metadata is None:
                continue
            record_profile, record_run_id, window = metadata
            if profile is not None and (record_profile != profile or record_run_id != run_id):
                errors.append(
                    f"line {record['line_number']}: identity {record_profile}/{record_run_id} "
                    f"does not match END {profile}/{run_id}"
                )
            if window_order and window < window_order[-1]:
                errors.append(
                    f"line {record['line_number']}: window ID regressed "
                    f"from {window_order[-1]} to {window}"
                )
            window_order.append(window)
            windows[window].append(record)

            if record["kind"] == "ownmap" and run_id is not None:
                line = str(record["line"])
                owner_ids = re.findall(r"(?:^|\s)I(\d+)(?=\s|$)", line)
                priorities = re.findall(r"(?:^|\s)P(\d{2})(?=\s|$)", line)
                addresses = re.findall(r"(?:^|\s)A([0-9A-Fa-f]{8})(?=\s|$)", line)
                if not owner_ids or not priorities or not addresses:
                    errors.append(f"line {record['line_number']}: malformed OWNMAP line")
                elif len(owner_ids) > 1 or len(priorities) > 1 or len(addresses) > 1:
                    errors.append(f"line {record['line_number']}: duplicate OWNMAP field")
                else:
                    owner_id = int(owner_ids[0])
                    owner_value = (int(priorities[0]), addresses[0].upper())
                    key = (run_id, owner_id)
                    if key in owner_maps and owner_maps[key] != owner_value:
                        errors.append(f"line {record['line_number']}: conflicting OWNMAP I{owner_id}")
                    owner_maps[key] = owner_value
                    run_ids_with_owner_maps.add(run_id)

        if windows and end_window != max(windows):
            errors.append(f"run {segment_index}: END W{end_window} != final window W{max(windows)}")
        identity = (profile, run_id) if profile is not None and run_id is not None else None
        if windows and identity is not None and identity in last_end_window_by_identity:
            previous_window = last_end_window_by_identity[identity]
            if min(windows) <= previous_window:
                errors.append(
                    f"run {segment_index}: PF{profile}/RID{run_id} window IDs restart/regress "
                    f"at W{min(windows)} after W{previous_window}"
                )
        if identity is not None and end_window is not None:
            last_end_window_by_identity[identity] = end_window
        if not records:
            errors.append(f"run {segment_index}: empty PERF run")

        accept_samples: list[dict[str, object]] = []
        for window, window_records in sorted(windows.items()):
            label = f"run {segment_index} W{window}"
            kinds = [str(record["kind"]) for record in window_records]
            if profile == "M2":
                result["m2_windows"] = int(result["m2_windows"]) + 1
                _exact_count(window_records, "summary", 1, label, errors)
                _exact_count(window_records, "hist", 1, label, errors)
                _exact_count(window_records, "draw", 1, label, errors)
                _exact_count(window_records, "m2i", 1, label, errors)
                if kinds.count("own") > 1:
                    errors.append(f"{label}: surplus OWN lines")
                if kinds.count("apb") > 1:
                    errors.append(f"{label}: surplus APB lines")
                unexpected = [
                    kind for kind in kinds
                    if kind not in {
                        "summary", "hist", "ownmap", "own", "draw", "m2i", "apb", "gpu"
                    }
                ]
                if unexpected:
                    errors.append(f"{label}: unexpected {','.join(unexpected)} line(s)")
            elif profile == "M3":
                result["m3_windows"] = int(result["m3_windows"]) + 1
                result["m3_sample_windows"] = int(result["m3_sample_windows"]) + kinds.count("m3s")
                _exact_count(window_records, "summary", 1, label, errors)
                _exact_count(window_records, "hist", 1, label, errors)
                _exact_count(window_records, "m3", 1, label, errors)
                _exact_count(window_records, "m3s", 1, label, errors)
                unexpected = [
                    kind for kind in kinds
                    if kind not in {"summary", "hist", "m3", "m3s"}
                ]
                if unexpected:
                    errors.append(f"{label}: unexpected {','.join(unexpected)} line(s)")
            elif profile in ACCEPT_PROFILES:
                result["accept_windows"] = int(result["accept_windows"]) + kinds.count("accept")
                _exact_count(window_records, "accept", 1, label, errors)
                if kinds.count("a1s") > 1:
                    errors.append(f"{label}: surplus A1S lines")
                if "a1s" in kinds and (
                    "accept" not in kinds or kinds.index("a1s") < kinds.index("accept")
                ):
                    errors.append(f"{label}: A1S must follow PERF ACCEPT")
                unexpected = [kind for kind in kinds if kind not in {"accept", "a1s"}]
                if unexpected:
                    errors.append(f"{label}: unexpected {','.join(unexpected)} line(s)")
            else:
                errors.append(f"{label}: unknown profile PF{profile}")

            summary_records = [record for record in window_records if record["kind"] == "summary"]
            frames: int | None = None
            if len(summary_records) == 1:
                summary_fields = _unique_numeric_values(
                    str(summary_records[0]["line"]),
                    ("S", "ST", "N"),
                    int(summary_records[0]["line_number"]),
                    "summary",
                    errors,
                )
                try:
                    frames = int(summary_fields["N"])
                    if frames <= 0:
                        raise ValueError
                except (KeyError, ValueError):
                    errors.append(f"line {summary_records[0]['line_number']}: summary missing valid N")

            hist_records = [record for record in window_records if record["kind"] == "hist"]
            if len(hist_records) == 1:
                histogram = _histogram_from_separate(str(hist_records[0]["line"]))
                if histogram is None:
                    errors.append(f"line {hist_records[0]['line_number']}: malformed 10-bucket histogram")
                elif frames is not None and sum(histogram) != frames:
                    errors.append(
                        f"line {hist_records[0]['line_number']}: histogram sum {sum(histogram)} != N{frames}"
                    )

            for record in window_records:
                line = str(record["line"])
                line_number = int(record["line_number"])
                if record["kind"] == "draw":
                    _validate_draw_line(line, line_number, errors)
                elif record["kind"] == "m2i":
                    _validate_m2i_line(
                        line, line_number, frames, run_id, owner_maps, errors
                    )
                elif record["kind"] == "apb":
                    _validate_apb_line(line, line_number, frames, errors)
                elif record["kind"] == "m3":
                    _validate_m3_line(line, line_number, frames, errors)
                elif record["kind"] == "m3s":
                    _validate_m3s_line(line, line_number, frames, errors)
                elif record["kind"] == "own":
                    owner_ids = [int(value) for value in re.findall(r"(?:^|\s)I(\d+)=", line)]
                    if not owner_ids:
                        errors.append(f"line {line_number}: empty OWN breakdown")
                    elif len(set(owner_ids)) != len(owner_ids):
                        errors.append(f"line {line_number}: duplicate OWN owner index")
                    for owner_id in owner_ids:
                        if run_id is not None and (run_id, owner_id) not in owner_maps:
                            errors.append(f"line {line_number}: OWN I{owner_id} has no OWNMAP in RID{run_id}")
                elif record["kind"] == "accept":
                    parsed = _unique_numeric_values(
                        line,
                        (
                            "S", "ST", "N", "AVG", "MAX", "P99", "AVGUS",
                            "MAXUS", "P99US", "OVR", "MISS", "V",
                        ),
                        line_number,
                        "ACCEPT",
                        errors,
                    )
                    histogram = _histogram_from_accept(line)
                    if parsed.get("V") != "1":
                        errors.append(f"line {line_number}: PERF-ACCEPT invalid latch")
                    try:
                        state = int(parsed["S"])
                        stage = int(parsed["ST"])
                        if state != 2:
                            errors.append(
                                f"line {line_number}: PERF-ACCEPT requires gameplay S2, got S{state}"
                            )
                        accept_frames = int(parsed["N"])
                        average_us = int(parsed["AVGUS"])
                        maximum_us = int(parsed["MAXUS"])
                        p99_us = int(parsed["P99US"])
                        average_ms = float(parsed["AVG"])
                        maximum_ms = float(parsed["MAX"])
                        p99_ms = float(parsed["P99"])
                        over_budget = int(parsed["OVR"])
                        misses = int(parsed["MISS"])
                        if histogram is None:
                            raise KeyError("H")
                        if sum(histogram) != accept_frames:
                            errors.append(
                                f"line {line_number}: histogram sum {sum(histogram)} != N{accept_frames}"
                            )
                        accept_samples.append(
                            {
                                "profile": profile, "run_id": run_id, "window": window,
                                "state": state, "stage": stage, "frames": accept_frames,
                                "avg_us": average_us, "max_us": maximum_us, "p99_us": p99_us,
                                "avg_ms": average_ms, "max_ms": maximum_ms, "p99_ms": p99_ms,
                                "over_budget": over_budget, "misses": misses,
                            }
                        )
                    except (KeyError, ValueError):
                        errors.append(f"line {line_number}: malformed PERF-ACCEPT line")
                elif record["kind"] == "a1s":
                    a1_sample = _validate_a1s_line(line, line_number, errors)
                    if a1_sample is not None:
                        a1_sample.update(
                            {"profile": profile, "run_id": run_id, "window": window}
                        )
                        a1_samples = result["a1_same_samples"]
                        assert isinstance(a1_samples, list)
                        a1_samples.append(a1_sample)
                        result["a1_same_windows"] = int(result["a1_same_windows"]) + 1

            if profile == "M3":
                m3_records = [record for record in window_records if record["kind"] == "m3"]
                m3s_records = [record for record in window_records if record["kind"] == "m3s"]
                if len(m3_records) == 1 and len(m3s_records) == 1:
                    m3_bt = re.findall(
                        r"(?:^|\s)BTUS(\d+)(?=\s|$)", str(m3_records[0]["line"])
                    )
                    m3s_raw = re.findall(
                        r"(?:^|\s)RAWUS(\d+)(?=\s|$)", str(m3s_records[0]["line"])
                    )
                    if len(m3_bt) == 1 and len(m3s_raw) == 1 and m3_bt[0] != m3s_raw[0]:
                        errors.append(
                            f"{label}: M3 BTUS{m3_bt[0]} != M3S RAWUS{m3s_raw[0]}"
                        )
                    m3_visits = re.findall(
                        r"(?:^|\s)VIS(\d+)(?=\s|$)", str(m3_records[0]["line"])
                    )
                    m3s_population = re.findall(
                        r"(?:^|\s)SAMP\d+/\d+/(\d+)(?=\s|$)",
                        str(m3s_records[0]["line"]),
                    )
                    if (
                        len(m3_visits) == 1
                        and len(m3s_population) == 1
                        and m3_visits[0] != m3s_population[0]
                    ):
                        errors.append(
                            f"{label}: M3 VIS{m3_visits[0]} != M3S emitter calls "
                            f"{m3s_population[0]}"
                        )

        if profile == "M2" and run_id is not None and run_id not in run_ids_with_owner_maps:
            errors.append(
                f"run {segment_index}: M2 has no OWNMAP for RID{run_id}; owner identity is unverifiable"
            )
        segment_summaries.append(
            {
                "profile": profile, "run_id": run_id, "end_window": end_window,
                "first_window": min(windows) if windows else None,
                "windows": windows, "accept_samples": accept_samples,
                "stage_sequence": _gameplay_stage_sequence(accept_samples),
            }
        )

    # The engine tears GameManager down at every stage, so one hardware
    # playthrough normally has three END-delimited segments. Merge only
    # adjacent segments with the same compiled profile/process nonce and
    # strictly increasing window IDs; a new RID always starts a new attempt.
    playthroughs: list[dict[str, object]] = []
    for segment in segment_summaries:
        can_merge = False
        if playthroughs:
            previous = playthroughs[-1]
            first_window = segment["first_window"]
            previous_end = previous["end_window"]
            previous_stages = previous["stage_sequence"]
            segment_stages = segment["stage_sequence"]
            assert isinstance(previous_stages, list)
            assert isinstance(segment_stages, list)
            starts_new_attempt = bool(segment_stages) and (
                previous_stages == [4, 5, 6]
                or bool(previous_stages) and segment_stages[0] <= previous_stages[-1]
            )
            can_merge = (
                segment["profile"] == previous["profile"]
                and segment["run_id"] == previous["run_id"]
                and isinstance(first_window, int)
                and isinstance(previous_end, int)
                and first_window > previous_end
                and not starts_new_attempt
            )
        if not can_merge:
            playthroughs.append(
                {
                    "profile": segment["profile"],
                    "run_id": segment["run_id"],
                    "first_window": segment["first_window"],
                    "end_window": segment["end_window"],
                    "accept_samples": list(segment["accept_samples"]),
                    "stage_sequence": list(segment["stage_sequence"]),
                }
            )
            continue
        previous = playthroughs[-1]
        previous_samples = previous["accept_samples"]
        assert isinstance(previous_samples, list)
        previous_samples.extend(segment["accept_samples"])
        previous["end_window"] = segment["end_window"]
        previous["stage_sequence"] = _gameplay_stage_sequence(previous_samples)

    result["playthroughs"] = len(playthroughs)
    latest = playthroughs[-1] if playthroughs else None
    if latest:
        result["latest_profile"] = latest["profile"]
        result["latest_run_id"] = latest["run_id"]
        result["latest_end_window"] = latest["end_window"]
        latest_samples = latest["accept_samples"]
        assert isinstance(latest_samples, list)
        result["accept_samples"] = latest_samples
        stage_sequence = _gameplay_stage_sequence(latest_samples)
        result["accept_stage_sequence"] = stage_sequence
        eligible = [
            sample for sample in latest_samples
            if sample["state"] == 2 and 4 <= int(sample["stage"]) <= 6
        ]
        result["accept_eligible_windows"] = len(eligible)

        if enforce_performance:
            if latest["profile"] != "ACCEPT":
                errors.append(f"performance acceptance requires PFACCEPT, got PF{latest['profile']}")
            for sample in eligible:
                line_label = f"W{sample['window']}"
                if int(sample["max_us"]) > 16700:
                    errors.append(f"{line_label}: MAXUS {sample['max_us']} exceeds 16700us")
                if int(sample["p99_us"]) > 15700:
                    errors.append(f"{line_label}: P99US {sample['p99_us']} exceeds 15700us")
                if int(sample["over_budget"]) != 0:
                    errors.append(f"{line_label}: {sample['over_budget']} over-budget frames")
                if int(sample["misses"]) != 0:
                    errors.append(f"{line_label}: {sample['misses']} VSync misses")
            if not eligible:
                errors.append("missing eligible gameplay PERF-ACCEPT windows (state 2, stage 4-6)")
            if stage_sequence != [4, 5, 6]:
                errors.append(
                    "latest merged gameplay playthrough does not cover stages 4,5,6 "
                    f"exactly: {stage_sequence}"
                )

    expected_profile = {"m2": "M2", "m3": "M3"}.get(expected)
    if expected_profile is not None and (latest is None or latest["profile"] != expected_profile):
        got = latest["profile"] if latest else None
        errors.append(f"latest run profile is PF{got}, expected PF{expected_profile}")
    if expected == "accept" and latest is not None and latest["profile"] not in ACCEPT_PROFILES:
        errors.append(f"latest run profile PF{latest['profile']} has no PERF-ACCEPT windows")
    if expected != "auto" and latest is None:
        errors.append("missing required complete PERF run")

    latest_samples = result["accept_samples"]
    assert isinstance(latest_samples, list)
    result["accept_max_ms"] = max(
        (float(sample["max_us"]) / 1000.0 for sample in latest_samples), default=0.0
    )
    result["accept_p99_ms"] = max(
        (float(sample["p99_us"]) / 1000.0 for sample in latest_samples), default=0.0
    )
    result["valid"] = not errors
    return result


def compare_accept(
    baseline: dict[str, object], probe: dict[str, object], same_source: bool = False,
) -> dict[str, object]:
    errors: list[str] = []
    baseline_samples = baseline["accept_samples"]
    probe_samples = probe["accept_samples"]
    assert isinstance(baseline_samples, list)
    assert isinstance(probe_samples, list)
    if not baseline["valid"]:
        errors.append("baseline PERF-ACCEPT log is invalid")
    if not probe["valid"]:
        errors.append("probe PERF-ACCEPT log is invalid")
    if same_source:
        errors.append("baseline and probe are the same file")
    if baseline.get("latest_profile") != "ACCEPT":
        errors.append(f"A/A baseline must be PFACCEPT, got PF{baseline.get('latest_profile')}")
    if probe.get("latest_profile") not in {"EMPTY_M2", "EMPTY_M3"}:
        errors.append(
            "A/A probe must be PFEMPTY_M2 or PFEMPTY_M3, "
            f"got PF{probe.get('latest_profile')}"
        )
    if baseline.get("latest_profile") == probe.get("latest_profile"):
        errors.append("baseline and probe use the same profile")
    if baseline.get("latest_run_id") == probe.get("latest_run_id"):
        errors.append("baseline and probe use the same RID/process run")
    if baseline.get("accept_stage_sequence") != [4, 5, 6]:
        errors.append(
            "A/A baseline latest playthrough must cover stages 4,5,6 exactly: "
            f"{baseline.get('accept_stage_sequence')}"
        )
    if probe.get("accept_stage_sequence") != [4, 5, 6]:
        errors.append(
            "A/A probe latest playthrough must cover stages 4,5,6 exactly: "
            f"{probe.get('accept_stage_sequence')}"
        )
    if len(baseline_samples) != len(probe_samples):
        errors.append(
            f"window count differs: baseline={len(baseline_samples)} probe={len(probe_samples)}"
        )

    worst_delta_us = 0
    for index, (baseline_sample, probe_sample) in enumerate(zip(baseline_samples, probe_samples), 1):
        assert isinstance(baseline_sample, dict)
        assert isinstance(probe_sample, dict)
        baseline_identity = (
            baseline_sample["window"], baseline_sample["state"],
            baseline_sample["stage"], baseline_sample["frames"],
        )
        probe_identity = (
            probe_sample["window"], probe_sample["state"],
            probe_sample["stage"], probe_sample["frames"],
        )
        if baseline_identity != probe_identity:
            errors.append(f"window {index} identity differs: {baseline_identity} != {probe_identity}")
            continue
        delta_us = abs(int(probe_sample["avg_us"]) - int(baseline_sample["avg_us"]))
        worst_delta_us = max(worst_delta_us, delta_us)
        if delta_us > 200:
            errors.append(f"window {index} A/A delta {delta_us}us exceeds 200us")
    return {
        "valid": not errors,
        "worst_avg_delta_ms": round(worst_delta_us / 1000.0, 6),
        "compared_windows": min(len(baseline_samples), len(probe_samples)),
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--expect", choices=("auto", "m2", "m3", "accept"), default="auto")
    parser.add_argument("--allow-missing-end", action="store_true")
    parser.add_argument("--compare-baseline", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        print(f"cannot read {args.log}: {error}", file=sys.stderr)
        return 2

    result = analyze(
        lines, args.expect, not args.allow_missing_end,
        False if args.compare_baseline else None,
    )
    if args.compare_baseline:
        try:
            baseline_lines = args.compare_baseline.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
        except OSError as error:
            print(f"cannot read {args.compare_baseline}: {error}", file=sys.stderr)
            return 2
        baseline = analyze(baseline_lines, "accept", not args.allow_missing_end, False)
        try:
            same_source = args.log.resolve() == args.compare_baseline.resolve()
        except OSError:
            same_source = args.log.absolute() == args.compare_baseline.absolute()
        result["aa_comparison"] = compare_accept(baseline, result, same_source=same_source)
        if not result["aa_comparison"]["valid"]:  # type: ignore[index]
            result["valid"] = False
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(
            f"valid={int(bool(result['valid']))} m2={result['m2_windows']} "
            f"m3={result['m3_windows']}/{result['m3_sample_windows']} "
            f"accept={result['accept_windows']} a1s={result['a1_same_windows']} "
            f"end={result['end_markers']}"
        )
        for error in result["errors"]:  # type: ignore[union-attr]
            print(f"ERROR: {error}", file=sys.stderr)
        comparison = result.get("aa_comparison")
        if isinstance(comparison, dict):
            print(
                f"A/A valid={int(bool(comparison['valid']))} "
                f"windows={comparison['compared_windows']} "
                f"worst={comparison['worst_avg_delta_ms']:.3f}ms"
            )
            for error in comparison["errors"]:
                print(f"ERROR: {error}", file=sys.stderr)
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

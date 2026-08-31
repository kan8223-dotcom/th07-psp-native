#!/usr/bin/env python3
"""Receive the diagnostic-only TH07 SHIKIGAMI UDP stream.

The receiver is deliberately passive: it listens on UDP 9996, appends every
accepted packet to a log, and never sends commands to the PSP.
"""

from __future__ import annotations

import argparse
import datetime as dt
import enum
import socket
import struct
import sys
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, TextIO


MAGIC = b"SKPS"
PROTOCOL_VERSION = 1
DEFAULT_BIND = "192.168.11.3"
DEFAULT_PORT = 9996
DEFAULT_LOST_AFTER = 3.0
DEFAULT_LOG = "shikigami_th07_telemetry.log"
DEFAULT_PERF_LOG = "TH07PSP_PERF_UDP.LOG"

HEADER = struct.Struct("!4sHHIIHHI")
IDENTITY = struct.Struct("!BBHI")
STATUS_BODY_V1 = struct.Struct("!HHI30I")
STATUS_BODY_V2 = struct.Struct("!HHI47I")
# Compatibility name used by schema-1 tooling and fixtures.
STATUS_BODY = STATUS_BODY_V1
EVENT_BODY = struct.Struct("!HH9I")
PORTRAIT_CACHE_BODY = struct.Struct("!HH83I")
PERF_LOG_BODY = struct.Struct("!HH8I4H")
ITEM_ME_BODY = struct.Struct("!HH9I")
A1_MOVE_BODY = struct.Struct("!HH10I")
HEADER_BYTES = 24
IDENTITY_BYTES = 8
STATUS_PAYLOAD_BYTES_V1 = 136
STATUS_PAYLOAD_BYTES_V2 = 204
# Compatibility name for the frozen schema-1 wire size.
STATUS_PAYLOAD_BYTES = STATUS_PAYLOAD_BYTES_V1
EVENT_PAYLOAD_BYTES = 48
PORTRAIT_CACHE_PAYLOAD_BYTES = 344
PERF_LOG_FIXED_PAYLOAD_BYTES = IDENTITY_BYTES + PERF_LOG_BODY.size
PERF_LOG_CHUNK_BYTES = 960
PERF_LOG_MAX_PAYLOAD_BYTES = PERF_LOG_FIXED_PAYLOAD_BYTES + PERF_LOG_CHUNK_BYTES
ITEM_ME_PAYLOAD_BYTES = 48
A1_MOVE_PAYLOAD_BYTES = 52
MAX_DATAGRAM_BYTES = HEADER_BYTES + PERF_LOG_MAX_PAYLOAD_BYTES


class ProtocolError(ValueError):
    pass


class PacketType(enum.IntEnum):
    HELLO = 1
    HEARTBEAT = 2
    SHUTDOWN = 3
    TH07_STATUS = 8
    TH07_EVENT = 9
    TH07_PORTRAIT_CACHE = 10
    TH07_PERF_LOG = 11
    TH07_ITEM_ME = 12
    TH07_A1_MOVE = 13


class EventType(enum.IntEnum):
    SUPERVISOR = 1
    STAGE = 2
    BGM = 3
    UNDERRUN = 4
    FATAL = 5
    GAME_FLAGS = 6


class ItemMeState(enum.IntEnum):
    UNAVAILABLE = 0
    TESTING = 1
    ENABLED = 2
    SAFE_FALLBACK = 3
    FAILED = 4


class ItemMeReason(enum.IntEnum):
    NONE = 0
    ME_UNAVAILABLE = 1
    SELFTEST_PASS = 2
    LIVE_ACQUIRE = 3
    LIVE_SUBMIT = 4
    LIVE_CONTRACT = 5
    AUTH_ACQUIRE = 6
    AUTH_SUBMIT = 7
    AUTH_CONTRACT = 8
    REJECT_ACQUIRE = 9
    REJECT_SUBMIT = 10
    REJECT_CONTRACT = 11
    BULLET_RETRY_FAILED = 12


class A1MoveReason(enum.IntEnum):
    NONE = 0
    ME_UNAVAILABLE = 1
    ITEM_DRAW_UNAVAILABLE = 2
    SELFTEST_PASS = 3
    BEGIN = 4
    BULLET_CONTRACT = 5
    ITEM_CONTRACT = 6
    BIT_MISMATCH = 7
    COMMON_FATAL = 8
    BULLET_RETRY_FAILED = 9


ITEM_ME_SCHEMA = 1
ITEM_ME_VALID_DECISION = 1 << 0
ITEM_ME_VALID_FAILURE_DETAIL = 1 << 1
ITEM_ME_VALID_KNOWN = ITEM_ME_VALID_DECISION | ITEM_ME_VALID_FAILURE_DETAIL

A1_MOVE_SCHEMA = 1
A1_MOVE_VALID_DECISION = 1 << 0
A1_MOVE_VALID_FAILURE_DETAIL = 1 << 1
A1_MOVE_VALID_KNOWN = (
    A1_MOVE_VALID_DECISION | A1_MOVE_VALID_FAILURE_DETAIL
)


PERF_LOG_SCHEMA = 1
PERF_LOG_BEGIN = 1 << 0
PERF_LOG_END = 1 << 1
PERF_LOG_VALID = 1 << 2
PERF_LOG_RETRY = 1 << 3
PERF_LOG_FLAGS_KNOWN = (
    PERF_LOG_BEGIN | PERF_LOG_END | PERF_LOG_VALID | PERF_LOG_RETRY
)
PERF_LOG_MAX_TOTAL_BYTES = 512 * 1024


VALID_MODEL_CAPACITY = 1 << 0
VALID_MAIN_HEAP_API = 1 << 1
VALID_GE_APERTURE_API = 1 << 2
VALID_GE_PRIOR_EVIDENCE = 1 << 3
VALID_FPS = 1 << 4
VALID_AUDIO_RING = 1 << 5
VALID_ME_PERF_WINDOW = 1 << 6
VALID_FRAME_PERF = 1 << 7
VALID_ME_NATIVE_ALLOCATOR = 1 << 8
VALID_ME_UPPER_OWNED = 1 << 9
VALID_AUDIO4M_USAGE = 1 << 10
VALID_GE_UPPER_OWNED = 1 << 11

STATUS_BGM_PLAYING = 1 << 0
STATUS_BGM_PAUSED = 1 << 1
STATUS_DEMO = 1 << 2
STATUS_REPLAY = 1 << 3
STATUS_GAME_PAUSED = 1 << 4
STATUS_BOSS = 1 << 5
STATUS_SPELL = 1 << 6
STATUS_DIALOGUE = 1 << 7
STATUS_FATAL_SEEN = 1 << 8
STATUS_GE_PRIOR_NOT_RUNTIME_OWNER = 1 << 11
STATUS_AUDIO4M_PROVEN = 1 << 12
STATUS_GE_UPPER_PORTRAIT = 1 << 13
STATUS_SFX_MAIN_RAM = 1 << 14
STATUS_BGM_MAIN_RAM = 1 << 15

AUDIO4M_PROOF_FULL_EXTENT = 1 << 0
AUDIO4M_PROOF_ATLAS_EXACT = 1 << 1
AUDIO4M_PROOF_CANONICAL_DAC = 1 << 2
AUDIO4M_PROOF_REPLICA_DAC = 1 << 3
AUDIO4M_PROOF_BGM_UPLOAD_WRAP = 1 << 4
AUDIO4M_PROOF_BGM_FETCH_WRAP = 1 << 5
AUDIO4M_PROOF_BGM_OUTPUT_WRAP = 1 << 6
AUDIO4M_PROOF_ZERO_FAULTS = 1 << 7
AUDIO4M_PROOF_REQUIRED = 0xFF
# SE mixes on the SC from Main RAM: the eDRAM atlas/DAC proof bits are
# structurally absent, so PROVEN needs only extent, BGM wraps and zero faults.
AUDIO4M_PROOF_REQUIRED_SFX_MAIN_RAM = 0xF1

# R18: the Main-RAM-SE profile owns only the 384 KiB BGM ring at
# 0x00010000..0x0006ffff.  This is wholly below the 2 MiB boundary; upper ME
# eDRAM is intentionally unowned after its retention faults were correlated
# with pause/resume and ring-wrap crackle.
AUDIO4M_EXTENT_BASE = 0x00010000
AUDIO4M_EXTENT_BYTES = 384 * 1024
AUDIO4M_RING_BYTES = 384 * 1024
AUDIO4M_SFX_ATLAS_BYTES = 2 * 1024 * 1024
AUDIO4M_SFX_REQUIRED_MASK = (1 << 30) - 1

PORTRAIT_CACHE_SCHEMA = 1
PORTRAIT_VALID_BRIDGE_STATE = 1 << 0
PORTRAIT_VALID_CACHE_SNAPSHOT = 1 << 1
PORTRAIT_VALID_KNOWN = (
    PORTRAIT_VALID_BRIDGE_STATE | PORTRAIT_VALID_CACHE_SNAPSHOT
)
PORTRAIT_SLOT_COUNT = 6
PORTRAIT_CAPACITY_MASK = (1 << PORTRAIT_SLOT_COUNT) - 1
PORTRAIT_PLAYER_MASK = 0x03
PORTRAIT_STAGE_MASK = 0x3C
PORTRAIT_CACHE_POOL_INITIALIZED = 1 << 0
PORTRAIT_CACHE_LEDGER_VALID = 1 << 1
PORTRAIT_CACHE_FLAGS_KNOWN = (
    PORTRAIT_CACHE_POOL_INITIALIZED | PORTRAIT_CACHE_LEDGER_VALID
)
PORTRAIT_ROLE_SELF = 1
PORTRAIT_ROLE_BOMB = 2
PORTRAIT_ROLE_STAGE_0 = 3
PORTRAIT_ROLE_STAGE_1 = 4
PORTRAIT_ROLE_STAGE_2 = 5
PORTRAIT_ROLE_STAGE_3 = 6
PORTRAIT_PSM_4444 = 2
PORTRAIT_POOL_RAW_BASE = 0x04200000
PORTRAIT_POOL_BYTES = 2 * 1024 * 1024
PORTRAIT_APERTURE_BYTES = 4 * 1024 * 1024
PORTRAIT_PLAYER_BYTES = 512 * 1024
PORTRAIT_STAGE_BYTES = 128 * 1024
PORTRAIT_MAX_LIVE_BYTES = 1536 * 1024
PORTRAIT_SLOT_EXPECTATIONS = (
    (PORTRAIT_ROLE_SELF, 25, PORTRAIT_PLAYER_BYTES, 512, 512),
    (PORTRAIT_ROLE_BOMB, 26, PORTRAIT_PLAYER_BYTES, 512, 512),
    (PORTRAIT_ROLE_STAGE_0, 28, PORTRAIT_STAGE_BYTES, 256, 256),
    (PORTRAIT_ROLE_STAGE_1, 29, PORTRAIT_STAGE_BYTES, 256, 256),
    (PORTRAIT_ROLE_STAGE_2, 30, PORTRAIT_STAGE_BYTES, 256, 256),
    (PORTRAIT_ROLE_STAGE_3, 31, PORTRAIT_STAGE_BYTES, 256, 256),
)
PORTRAIT_ROLE_NAMES = {
    PORTRAIT_ROLE_SELF: "SELF",
    PORTRAIT_ROLE_BOMB: "BOMB",
    PORTRAIT_ROLE_STAGE_0: "STAGE0",
    PORTRAIT_ROLE_STAGE_1: "STAGE1",
    PORTRAIT_ROLE_STAGE_2: "STAGE2",
    PORTRAIT_ROLE_STAGE_3: "STAGE3",
}


def _portrait_required_mask(stage_portrait_count: int) -> int:
    if not 1 <= stage_portrait_count <= 4:
        raise ValueError("stage portrait count must be in 1..4")
    return PORTRAIT_PLAYER_MASK | (
        ((1 << stage_portrait_count) - 1) << 2
    )


def _portrait_stage_count(required_mask: int) -> int | None:
    """Return the committed contiguous stage-role count, or None if invalid."""
    if required_mask & PORTRAIT_PLAYER_MASK != PORTRAIT_PLAYER_MASK:
        return None
    stage_bits = (required_mask & PORTRAIT_STAGE_MASK) >> 2
    for count in range(1, 5):
        if stage_bits == (1 << count) - 1:
            return count
    return None


def _portrait_expected_live_bytes(required_mask: int) -> int:
    return sum(
        expected[2]
        for index, expected in enumerate(PORTRAIT_SLOT_EXPECTATIONS)
        if required_mask & (1 << index)
    )


def _signed(value: int) -> int:
    return value - 0x1_0000_0000 if value & 0x8000_0000 else value


@dataclass(frozen=True)
class Identity:
    model: int
    profile: int
    state: int
    local_ipv4: int

    @property
    def local_ip(self) -> str:
        return socket.inet_ntoa(struct.pack("!I", self.local_ipv4))


@dataclass(frozen=True)
class Status:
    schema: int
    valid: int
    flags: int
    frame: int
    supervisor_state: int
    stage: int
    fps_x10: int
    main_physical: int
    main_free: int
    main_maxfree: int
    ge_physical: int
    ge_aperture: int
    ge_prior_base: int
    ge_prior_bytes: int
    me_physical: int
    me_allocator_managed: int
    me_upper_base: int
    me_upper_bytes: int
    bgm_ring_bytes: int
    bgm_fill_frames: int
    underruns: int
    audio_generation: int
    bgm_index: int
    me_jobs: int
    me_fallbacks: int
    me_timeouts: int
    me_max_wait_us: int
    spell_index: int
    fatal_count: int
    fatal_hash: int
    cpu_time_x10: int
    ge_wait_x10: int
    io_wait_x10: int
    sfx_atlas_bytes: int = 0
    sfx_canonical_bytes: int = 0
    sfx_replica_bytes: int = 0
    sfx_canonical_output_mask: int = 0
    sfx_replica_output_mask: int = 0
    sfx_mix_jobs: int = 0
    sfx_output_blocks: int = 0
    sfx_fifo_misses: int = 0
    sfx_fatal: int = 0
    sfx_coverage_active: int = 0
    sfx_coverage_complete: int = 0
    sfx_coverage_pass: int = 0
    sfx_coverage_buffer: int = 0
    bgm_upload_wraps: int = 0
    bgm_fetch_wraps: int = 0
    bgm_output_wraps: int = 0
    audio4m_proof_flags: int = 0


@dataclass(frozen=True)
class Event:
    schema: int
    event_type: EventType
    event_count: int
    old_value: int
    new_value: int
    frame: int
    supervisor_state: int
    stage: int
    bgm_index: int
    underruns: int
    fatal_hash: int


@dataclass(frozen=True)
class PortraitSlot:
    role: int
    texture_slot: int
    raw_address: int
    allocation_bytes: int
    width: int
    height: int
    psm: int
    source_hash: int
    readback_hash: int
    upload_generation: int
    draw_count: int


@dataclass(frozen=True)
class PortraitCache:
    schema: int
    valid: int
    bridge_active: int
    power_lock_held: int
    live_aperture_bytes: int
    flags: int
    cache_generation: int
    stage: int
    required_mask: int
    owned_mask: int
    verified_mask: int
    sampled_mask: int
    pool_raw_base: int
    pool_bytes: int
    live_bytes: int
    fallback_count: int
    migration_count: int
    allocation_failure_count: int
    invariant_failure_count: int
    slots: tuple[PortraitSlot, ...]


@dataclass(frozen=True)
class ItemMe:
    schema: int
    valid: int
    state: ItemMeState
    reason: ItemMeReason
    item_selftest_runs: int
    item_selftest_failures: int
    bullet_retry_runs: int
    bullet_retry_passes: int
    last_wait_result: int
    last_stream_result: int
    last_item_result: int


@dataclass(frozen=True)
class A1Move:
    schema: int
    valid: int
    state: ItemMeState
    reason: A1MoveReason
    selftest_runs: int
    selftest_failures: int
    bullet_retry_runs: int
    bullet_retry_passes: int
    last_poll_result: int
    last_bullet_result: int
    last_item_result: int
    first_mismatch_slot: int


@dataclass(frozen=True)
class PerfLogChunk:
    schema: int
    flags: int
    run_id: int
    snapshot_id: int
    window_id: int
    total_bytes: int
    offset: int
    log_crc32: int
    chunk_crc32: int
    dropped_lines: int
    chunk_index: int
    chunk_count: int
    data_bytes: int
    data: bytes


@dataclass(frozen=True)
class Packet:
    packet_type: PacketType
    sequence: int
    uptime_ms: int
    flags: int
    build_id: int
    identity: Identity
    status: Optional[Status] = None
    event: Optional[Event] = None
    portrait_cache: Optional[PortraitCache] = None
    item_me: Optional[ItemMe] = None
    a1_move: Optional[A1Move] = None
    perf_log: Optional[PerfLogChunk] = None


def _validate_schema1_status(status: Status) -> None:
    if status.valid & VALID_GE_UPPER_OWNED or status.flags & STATUS_GE_UPPER_PORTRAIT:
        raise ProtocolError("schema 1 advertised runtime GE upper ownership")
    if status.valid & VALID_AUDIO_RING and status.bgm_ring_bytes != 393216:
        raise ProtocolError("TH07 status changed the canonical 384 KiB ring")
    if not status.valid & VALID_ME_UPPER_OWNED and (
        status.me_upper_base != 0 or status.me_upper_bytes != 0
    ):
        raise ProtocolError("unowned ME upper extent contains an address")
    if status.valid & VALID_ME_UPPER_OWNED and (
        status.me_upper_base != 0x00200000 or status.me_upper_bytes != 393216
    ):
        raise ProtocolError("MECC backend changed the proven 384 KiB extent")
    if status.valid & VALID_AUDIO4M_USAGE or status.flags & STATUS_AUDIO4M_PROVEN:
        raise ProtocolError("schema 1 advertised AUDIO4M state")


def _audio4m_zero_faults(status: Status) -> bool:
    return (
        status.sfx_fifo_misses == 0
        and status.sfx_fatal == 0
        and status.underruns == 0
        and status.me_fallbacks == 0
        and status.me_timeouts == 0
        and status.fatal_count == 0
    )


def _audio4m_sfx_idle(status: Status) -> bool:
    """All eDRAM SFX machinery reports empty: the SE-Main-RAM shape."""
    return (
        status.sfx_atlas_bytes == 0
        and status.sfx_canonical_bytes == 0
        and status.sfx_replica_bytes == 0
        and status.sfx_canonical_output_mask == 0
        and status.sfx_replica_output_mask == 0
        and status.sfx_mix_jobs == 0
        and status.sfx_output_blocks == 0
        and status.sfx_fifo_misses == 0
        and status.sfx_fatal == 0
        and status.sfx_coverage_active == 0
        and status.sfx_coverage_complete == 0
        and status.sfx_coverage_pass == 0
        and status.sfx_coverage_buffer == 0
    )


def _audio4m_runtime_active(status: Status) -> bool:
    """Infer normal proven-extent use without turning it into a PROVEN claim."""
    bgm_main_ram = bool(status.flags & STATUS_BGM_MAIN_RAM)
    if bgm_main_ram:
        exact_backend = (
            not status.valid & VALID_ME_UPPER_OWNED
            and status.me_upper_base == 0
            and status.me_upper_bytes == 0
        )
    else:
        exact_backend = (
            bool(status.valid & VALID_ME_UPPER_OWNED)
            and status.me_upper_base == AUDIO4M_EXTENT_BASE
            and status.me_upper_bytes == AUDIO4M_EXTENT_BYTES
        )
    if status.flags & STATUS_SFX_MAIN_RAM:
        exact_atlas = _audio4m_sfx_idle(status)
        sfx_activity = True
    else:
        exact_atlas = (
            status.sfx_atlas_bytes == AUDIO4M_SFX_ATLAS_BYTES
            and status.sfx_canonical_bytes > 0
            and status.sfx_replica_bytes > 0
            and status.sfx_canonical_bytes + status.sfx_replica_bytes
            == AUDIO4M_SFX_ATLAS_BYTES
        )
        sfx_activity = status.sfx_mix_jobs > 0 and status.sfx_output_blocks > 0
    bgm_activity = (
        (
            bool(status.flags & STATUS_BGM_PLAYING)
            and status.bgm_fill_frames > 0
            and (bgm_main_ram or status.me_jobs > 0)
        )
        or status.bgm_upload_wraps > 0
        or status.bgm_fetch_wraps > 0
        or status.bgm_output_wraps > 0
    )
    return (
        exact_backend
        and status.bgm_ring_bytes == AUDIO4M_RING_BYTES
        and exact_atlas
        and sfx_activity
        and bgm_activity
        and _audio4m_zero_faults(status)
        and status.sfx_coverage_active == 0
        and status.sfx_coverage_complete == 0
    )


def _validate_schema2_status(status: Status) -> None:
    if not status.valid & VALID_AUDIO4M_USAGE:
        raise ProtocolError("schema 2 lacks AUDIO4M usage validity")
    if not status.valid & VALID_AUDIO_RING or status.bgm_ring_bytes != AUDIO4M_RING_BYTES:
        raise ProtocolError("AUDIO4M status changed the proven 384 KiB BGM ring")
    if status.bgm_fill_frames > status.bgm_ring_bytes // 4:
        raise ProtocolError("AUDIO4M BGM fill exceeds its ring")
    owned = bool(status.valid & VALID_ME_UPPER_OWNED)
    bgm_main_ram = bool(status.flags & STATUS_BGM_MAIN_RAM)
    if bgm_main_ram and owned:
        raise ProtocolError("Main-RAM BGM status also claims ME eDRAM ownership")
    if owned and (
        status.me_upper_base != AUDIO4M_EXTENT_BASE
        or status.me_upper_bytes != AUDIO4M_EXTENT_BYTES
    ):
        raise ProtocolError("AUDIO4M status changed the lower 384 KiB owned extent")
    if not owned and (status.me_upper_base != 0 or status.me_upper_bytes != 0):
        raise ProtocolError("unowned AUDIO4M extent contains an address")

    ge_owned = bool(status.valid & VALID_GE_UPPER_OWNED)
    ge_prior = bool(status.valid & VALID_GE_PRIOR_EVIDENCE)
    ge_portrait = bool(status.flags & STATUS_GE_UPPER_PORTRAIT)
    ge_no_owner = bool(status.flags & STATUS_GE_PRIOR_NOT_RUNTIME_OWNER)
    if ge_owned:
        if ge_prior or ge_no_owner or not ge_portrait:
            raise ProtocolError("owned GE upper extent has contradictory ownership state")
        if status.ge_prior_base != 0x04200000 or status.ge_prior_bytes != 2 * 1024 * 1024:
            raise ProtocolError("portrait cache changed the exact upper GE extent")
    elif ge_portrait:
        raise ProtocolError("unowned GE upper extent claims PORTRAIT")
    elif ge_prior:
        if not ge_no_owner:
            raise ProtocolError("prior GE evidence lacks NO-OWNER status")
        if status.ge_prior_base != 0x04200000 or status.ge_prior_bytes != 2 * 1024 * 1024:
            raise ProtocolError("prior GE evidence changed the upper extent")
    elif status.ge_prior_base != 0 or status.ge_prior_bytes != 0:
        raise ProtocolError("unknown GE upper extent contains an address")

    if (
        status.sfx_atlas_bytes > AUDIO4M_SFX_ATLAS_BYTES
        or status.sfx_canonical_bytes > AUDIO4M_SFX_ATLAS_BYTES
        or status.sfx_replica_bytes > AUDIO4M_SFX_ATLAS_BYTES
        or status.sfx_canonical_bytes + status.sfx_replica_bytes
        != status.sfx_atlas_bytes
    ):
        raise ProtocolError("AUDIO4M SFX atlas byte accounting is contradictory")
    if (
        status.sfx_canonical_output_mask & ~AUDIO4M_SFX_REQUIRED_MASK
        or status.sfx_replica_output_mask & ~AUDIO4M_SFX_REQUIRED_MASK
    ):
        raise ProtocolError("AUDIO4M SFX output mask exceeds 30 buffers")
    if status.audio4m_proof_flags & ~AUDIO4M_PROOF_REQUIRED:
        raise ProtocolError("AUDIO4M proof contains unknown bits")
    if status.sfx_coverage_active not in (0, 1) or status.sfx_coverage_complete not in (0, 1):
        raise ProtocolError("AUDIO4M coverage state is not boolean")
    if status.sfx_coverage_pass not in (0, 1):
        raise ProtocolError("AUDIO4M coverage pass state is not boolean")
    if status.sfx_coverage_buffer > 30:
        raise ProtocolError("AUDIO4M coverage buffer exceeds 30 buffers")
    if status.sfx_coverage_active and status.sfx_coverage_complete:
        raise ProtocolError("AUDIO4M coverage is both active and complete")

    proof = status.audio4m_proof_flags
    if not owned and (
        proof & AUDIO4M_PROOF_FULL_EXTENT or status.flags & STATUS_AUDIO4M_PROVEN
    ):
        raise ProtocolError("unowned AUDIO4M status claims full-extent proof")
    sfx_main_ram = bool(status.flags & STATUS_SFX_MAIN_RAM)
    if sfx_main_ram:
        if not _audio4m_sfx_idle(status):
            raise ProtocolError("SE-Main-RAM status carries eDRAM SFX state")
        if proof & (
            AUDIO4M_PROOF_ATLAS_EXACT
            | AUDIO4M_PROOF_CANONICAL_DAC
            | AUDIO4M_PROOF_REPLICA_DAC
        ):
            raise ProtocolError("SE-Main-RAM status claims eDRAM SFX proof")
    required_proof = (
        AUDIO4M_PROOF_REQUIRED_SFX_MAIN_RAM if sfx_main_ram else AUDIO4M_PROOF_REQUIRED
    )
    exact_atlas = (
        status.sfx_atlas_bytes == AUDIO4M_SFX_ATLAS_BYTES
        and status.sfx_canonical_bytes > 0
        and status.sfx_replica_bytes > 0
        and status.sfx_canonical_bytes + status.sfx_replica_bytes
        == AUDIO4M_SFX_ATLAS_BYTES
    )
    if proof & AUDIO4M_PROOF_ATLAS_EXACT and not exact_atlas:
        raise ProtocolError("AUDIO4M atlas proof bit is unsupported by telemetry")
    if proof & AUDIO4M_PROOF_CANONICAL_DAC and (
        status.sfx_canonical_output_mask != AUDIO4M_SFX_REQUIRED_MASK
    ):
        raise ProtocolError("AUDIO4M canonical-DAC proof bit is unsupported")
    if proof & AUDIO4M_PROOF_REPLICA_DAC and (
        status.sfx_replica_output_mask != AUDIO4M_SFX_REQUIRED_MASK
    ):
        raise ProtocolError("AUDIO4M replica-DAC proof bit is unsupported")
    if proof & AUDIO4M_PROOF_BGM_UPLOAD_WRAP and status.bgm_upload_wraps == 0:
        raise ProtocolError("AUDIO4M upload-wrap proof bit is unsupported")
    if proof & AUDIO4M_PROOF_BGM_FETCH_WRAP and status.bgm_fetch_wraps == 0:
        raise ProtocolError("AUDIO4M fetch-wrap proof bit is unsupported")
    if proof & AUDIO4M_PROOF_BGM_OUTPUT_WRAP and status.bgm_output_wraps == 0:
        raise ProtocolError("AUDIO4M output-wrap proof bit is unsupported")
    if proof & AUDIO4M_PROOF_ZERO_FAULTS and not _audio4m_zero_faults(status):
        raise ProtocolError("AUDIO4M zero-fault proof bit is unsupported")

    proven = bool(status.flags & STATUS_AUDIO4M_PROVEN)
    if proof == required_proof and not proven:
        raise ProtocolError("AUDIO4M proof is complete without PROVEN status")
    if not proven:
        return
    if proof != required_proof:
        raise ProtocolError("AUDIO4M PROVEN status lacks all proof bits")
    if not sfx_main_ram:
        if not exact_atlas:
            raise ProtocolError("AUDIO4M PROVEN status lacks the exact SFX atlas")
        if (
            status.sfx_canonical_output_mask != AUDIO4M_SFX_REQUIRED_MASK
            or status.sfx_replica_output_mask != AUDIO4M_SFX_REQUIRED_MASK
        ):
            raise ProtocolError("AUDIO4M PROVEN status lacks all DAC output masks")
        if (
            status.sfx_coverage_complete != 1
            or status.sfx_coverage_active != 0
            or status.sfx_coverage_pass != 1
            or status.sfx_coverage_buffer != 30
        ):
            raise ProtocolError("AUDIO4M PROVEN status lacks completed coverage")
    if (
        status.bgm_upload_wraps == 0
        or status.bgm_fetch_wraps == 0
        or status.bgm_output_wraps == 0
    ):
        raise ProtocolError("AUDIO4M PROVEN status lacks all BGM wraps")
    if not _audio4m_zero_faults(status):
        raise ProtocolError("AUDIO4M PROVEN status contains an audio fault")


def _portrait_cache_words(value: PortraitCache) -> tuple[int, ...]:
    words = (
        value.flags,
        value.cache_generation,
        value.stage,
        value.required_mask,
        value.owned_mask,
        value.verified_mask,
        value.sampled_mask,
        value.pool_raw_base,
        value.pool_bytes,
        value.live_bytes,
        value.fallback_count,
        value.migration_count,
        value.allocation_failure_count,
        value.invariant_failure_count,
    )
    return words + tuple(
        word
        for slot in value.slots
        for word in (
            slot.role,
            slot.texture_slot,
            slot.raw_address,
            slot.allocation_bytes,
            slot.width,
            slot.height,
            slot.psm,
            slot.source_hash,
            slot.readback_hash,
            slot.upload_generation,
            slot.draw_count,
        )
    )


def _validate_portrait_cache_wire(value: PortraitCache) -> None:
    """Reject malformed type-10 data without turning runtime progress into proof."""
    if value.schema != PORTRAIT_CACHE_SCHEMA:
        raise ProtocolError(f"unsupported TH07 portrait cache schema {value.schema}")
    if value.valid & ~PORTRAIT_VALID_KNOWN:
        raise ProtocolError("TH07 portrait cache contains unknown validity bits")
    if value.bridge_active not in (0, 1) or value.power_lock_held not in (0, 1):
        raise ProtocolError("TH07 portrait bridge state is not boolean")
    if not value.valid & PORTRAIT_VALID_BRIDGE_STATE and (
        value.bridge_active or value.power_lock_held or value.live_aperture_bytes
    ):
        raise ProtocolError("invalid TH07 portrait bridge fields are nonzero")
    if len(value.slots) != PORTRAIT_SLOT_COUNT:
        raise ProtocolError("TH07 portrait cache slot count changed")
    if not value.valid & PORTRAIT_VALID_CACHE_SNAPSHOT:
        if any(_portrait_cache_words(value)):
            raise ProtocolError("invalid TH07 portrait cache snapshot is nonzero")
        return
    if value.flags & ~PORTRAIT_CACHE_FLAGS_KNOWN:
        raise ProtocolError("TH07 portrait cache contains unknown flag bits")
    for name, mask in (
        ("required", value.required_mask),
        ("owned", value.owned_mask),
        ("verified", value.verified_mask),
        ("sampled", value.sampled_mask),
    ):
        if mask & ~PORTRAIT_CAPACITY_MASK:
            raise ProtocolError(f"TH07 portrait {name} mask exceeds six slots")
    for index, slot in enumerate(value.slots):
        if slot.role > PORTRAIT_ROLE_STAGE_3:
            raise ProtocolError(f"TH07 portrait slot {index} has an unknown role")
        if slot.allocation_bytes == 0 and slot.raw_address != 0:
            raise ProtocolError(
                f"TH07 portrait slot {index} has an address without an allocation"
            )


def _portrait_cache_assessment(
    value: PortraitCache, identity: Identity
) -> tuple[str, tuple[str, ...]]:
    """Evaluate one self-contained snapshot; never combine it with STATUS."""
    failures: list[str] = []
    incomplete: list[str] = []

    if identity.model != 3:
        failures.append(f"model {identity.model} is not PSP-3000/04g")
    bridge_valid = bool(value.valid & PORTRAIT_VALID_BRIDGE_STATE)
    cache_valid = bool(value.valid & PORTRAIT_VALID_CACHE_SNAPSHOT)
    if not bridge_valid:
        incomplete.append("bridge state unavailable")
    if not cache_valid:
        incomplete.append("cache snapshot unavailable")
    if not bridge_valid or not cache_valid:
        return ("FAIL", tuple(failures)) if failures else (
            "INCOMPLETE",
            tuple(incomplete),
        )

    if not value.bridge_active:
        incomplete.append("GE bridge inactive")
    if value.bridge_active and not value.power_lock_held:
        failures.append("active GE bridge lacks the power lock")
    elif not value.power_lock_held:
        incomplete.append("GE power lock is not held")
    if value.live_aperture_bytes != PORTRAIT_APERTURE_BYTES:
        if not value.bridge_active and value.live_aperture_bytes in (
            0,
            2 * 1024 * 1024,
        ):
            incomplete.append("GE aperture has not widened to 4 MiB")
        else:
            failures.append(
                f"live GE aperture is {value.live_aperture_bytes} bytes"
            )

    if value.flags != PORTRAIT_CACHE_FLAGS_KNOWN:
        missing = PORTRAIT_CACHE_FLAGS_KNOWN & ~value.flags
        incomplete.append(f"cache flags missing 0x{missing:02X}")
    if value.cache_generation == 0:
        if value.flags or value.owned_mask or value.verified_mask:
            failures.append("cache generation is zero")
        else:
            incomplete.append("cache generation is zero")
    if value.stage == 0:
        incomplete.append("stage is not active")
    elif not 1 <= value.stage <= 8:
        failures.append(f"stage {value.stage} is outside 1..8")

    # required_mask is a commit marker as well as the exact per-stage set.
    # Zero is expected during initial load and between stages.  A nonzero mask
    # is published only after LoadAnms(face_XX_00.anm) succeeds, so omissions
    # after that point are failures rather than an inferred smaller stage.
    prewarm_complete = value.required_mask != 0
    stage_portrait_count = _portrait_stage_count(value.required_mask)
    if not prewarm_complete:
        incomplete.append("portrait prewarm is not committed")
    elif stage_portrait_count is None:
        failures.append(
            f"required mask 0x{value.required_mask:02X} is not player + "
            "1..4 contiguous stage roles"
        )
    if prewarm_complete and value.owned_mask & ~value.required_mask:
        failures.append("owned mask exceeds the committed required mask")
    if value.verified_mask & ~value.owned_mask:
        failures.append("verified mask exceeds owned mask")
    if value.sampled_mask & ~value.owned_mask:
        failures.append("sampled mask exceeds owned mask")
    if prewarm_complete and value.owned_mask != value.required_mask:
        failures.append(
            f"prewarm complete with owned mask 0x{value.owned_mask:02X}, "
            f"expected 0x{value.required_mask:02X}"
        )
    if prewarm_complete and value.verified_mask != value.required_mask:
        failures.append(
            f"prewarm complete with verified mask 0x{value.verified_mask:02X}, "
            f"expected 0x{value.required_mask:02X}"
        )

    if value.pool_raw_base == 0 and value.pool_bytes == 0:
        incomplete.append("upper portrait pool is not initialized")
    elif (
        value.pool_raw_base != PORTRAIT_POOL_RAW_BASE
        or value.pool_bytes != PORTRAIT_POOL_BYTES
    ):
        failures.append(
            f"pool is 0x{value.pool_raw_base:08X}+{value.pool_bytes}, not "
            "0x04200000+2097152"
        )

    fault_counts = (
        ("fallback", value.fallback_count),
        ("migration", value.migration_count),
        ("allocation failure", value.allocation_failure_count),
        ("invariant failure", value.invariant_failure_count),
    )
    for name, count in fault_counts:
        if count:
            failures.append(f"{name} count is {count}")

    ranges: list[tuple[int, int, int]] = []
    allocation_sum = 0
    expected_pool_end = PORTRAIT_POOL_RAW_BASE + PORTRAIT_POOL_BYTES
    for index, (slot, expected) in enumerate(
        zip(value.slots, PORTRAIT_SLOT_EXPECTATIONS)
    ):
        (
            expected_role,
            expected_texture_slot,
            expected_bytes,
            expected_width,
            expected_height,
        ) = expected
        bit = 1 << index
        required = bool(value.required_mask & bit)
        if slot.role == 0:
            incomplete.append(f"slot {index} role is not published")
        elif slot.role != expected_role:
            failures.append(
                f"slot {index} role is {slot.role}, expected {expected_role}"
            )
        if slot.texture_slot == 0:
            incomplete.append(f"slot {index} texture slot is not published")
        elif slot.texture_slot != expected_texture_slot:
            failures.append(
                f"slot {index} texture slot is {slot.texture_slot}, "
                f"expected {expected_texture_slot}"
            )
        if slot.allocation_bytes == 0:
            if value.owned_mask & bit:
                failures.append(f"owned slot {index} has no allocation")
            elif required:
                failures.append(f"required slot {index} is not allocated")
            if slot.raw_address or slot.width or slot.height or slot.psm:
                failures.append(f"unallocated slot {index} retains image metadata")
            if slot.source_hash or slot.readback_hash or slot.upload_generation:
                failures.append(f"unallocated slot {index} retains upload metadata")
        else:
            allocation_sum += slot.allocation_bytes
            if not value.owned_mask & bit:
                failures.append(f"allocated slot {index} is not owned")
            if slot.allocation_bytes != expected_bytes:
                failures.append(
                    f"slot {index} allocation is {slot.allocation_bytes} bytes"
                )
            end = slot.raw_address + slot.allocation_bytes
            if end > 0x1_0000_0000 or end <= slot.raw_address:
                failures.append(f"slot {index} range wraps")
            elif (
                slot.raw_address < PORTRAIT_POOL_RAW_BASE
                or end > expected_pool_end
            ):
                failures.append(f"slot {index} range is outside the upper pool")
            else:
                ranges.append((slot.raw_address, end, index))
            if slot.raw_address & 0xFFF:
                failures.append(f"slot {index} address is not 4 KiB aligned")
            if slot.width == 0 or slot.height == 0:
                failures.append(f"allocated slot {index} has no dimensions")
            elif slot.width != expected_width or slot.height != expected_height:
                failures.append(
                    f"slot {index} dimensions are {slot.width}x{slot.height}"
                )
        if slot.allocation_bytes and slot.psm != PORTRAIT_PSM_4444:
            failures.append(f"slot {index} PSM is {slot.psm}, not RGBA4444")
        if slot.source_hash == 0 and slot.readback_hash == 0:
            if value.verified_mask & bit:
                failures.append(f"verified slot {index} has no hashes")
            elif slot.allocation_bytes:
                incomplete.append(f"slot {index} has not been hash-verified")
        elif slot.source_hash == 0 or slot.readback_hash == 0:
            failures.append(f"slot {index} has only one hash")
        elif slot.source_hash != slot.readback_hash:
            failures.append(f"slot {index} source/readback hash mismatch")
        elif not value.verified_mask & bit:
            failures.append(f"hash-verified slot {index} lacks the verified bit")
        if slot.upload_generation == 0:
            if slot.allocation_bytes or value.verified_mask & bit:
                if prewarm_complete and required:
                    failures.append(f"slot {index} upload generation is zero")
                else:
                    incomplete.append(f"slot {index} upload is still pending")

    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if current[0] < previous[1]:
            failures.append(f"slots {previous[2]} and {current[2]} overlap")
    if value.live_bytes > PORTRAIT_MAX_LIVE_BYTES:
        failures.append(
            f"live portrait bytes exceed the {PORTRAIT_MAX_LIVE_BYTES}-byte budget"
        )
    if prewarm_complete:
        expected_live_bytes = _portrait_expected_live_bytes(value.required_mask)
        if value.live_bytes != expected_live_bytes:
            failures.append(
                f"prewarm complete with {value.live_bytes} live portrait bytes, "
                f"expected {expected_live_bytes}"
            )
    elif value.live_bytes == 0:
        incomplete.append("live portrait bytes are zero")
    if value.live_bytes != allocation_sum:
        failures.append(
            f"live byte ledger {value.live_bytes} != slot sum {allocation_sum}"
        )

    if failures:
        return "FAIL", tuple(dict.fromkeys(failures))
    if incomplete:
        return "INCOMPLETE", tuple(dict.fromkeys(incomplete))
    return "PASS", ()


def _validate_perf_log_chunk(value: PerfLogChunk) -> None:
    if value.schema != PERF_LOG_SCHEMA:
        raise ProtocolError(f"unsupported TH07 perf log schema {value.schema}")
    if value.flags & ~PERF_LOG_FLAGS_KNOWN:
        raise ProtocolError("TH07 perf log contains unknown flags")
    if value.run_id == 0 or value.snapshot_id == 0:
        raise ProtocolError("TH07 perf log has a zero run/snapshot id")
    if value.total_bytes > PERF_LOG_MAX_TOTAL_BYTES:
        raise ProtocolError("TH07 perf log exceeds the diagnostic RAM buffer")
    expected_count = max(
        1, (value.total_bytes + PERF_LOG_CHUNK_BYTES - 1) // PERF_LOG_CHUNK_BYTES
    )
    if value.chunk_count != expected_count:
        raise ProtocolError("TH07 perf log chunk count is inconsistent")
    if value.chunk_index >= value.chunk_count:
        raise ProtocolError("TH07 perf log chunk index is out of range")
    expected_offset = value.chunk_index * PERF_LOG_CHUNK_BYTES
    if value.offset != expected_offset or value.offset > value.total_bytes:
        raise ProtocolError("TH07 perf log chunk offset is inconsistent")
    expected_bytes = min(
        PERF_LOG_CHUNK_BYTES, value.total_bytes - value.offset
    )
    if value.offset + value.data_bytes > value.total_bytes:
        raise ProtocolError("TH07 perf log chunk exceeds total bytes")
    if value.data_bytes != expected_bytes or len(value.data) != expected_bytes:
        raise ProtocolError("TH07 perf log chunk length is inconsistent")
    if bool(value.flags & PERF_LOG_BEGIN) != (value.chunk_index == 0):
        raise ProtocolError("TH07 perf log BEGIN flag is inconsistent")
    if bool(value.flags & PERF_LOG_END) != (
        value.chunk_index + 1 == value.chunk_count
    ):
        raise ProtocolError("TH07 perf log END flag is inconsistent")
    if value.flags & PERF_LOG_VALID and value.dropped_lines != 0:
        raise ProtocolError("valid TH07 perf log reports dropped lines")
    if zlib.crc32(value.data) & 0xFFFF_FFFF != value.chunk_crc32:
        raise ProtocolError("TH07 perf log chunk CRC mismatch")


def _validate_item_me(value: ItemMe) -> None:
    if value.schema != ITEM_ME_SCHEMA:
        raise ProtocolError(f"unsupported TH07 Item ME schema {value.schema}")
    if value.valid & ~ITEM_ME_VALID_KNOWN:
        raise ProtocolError("TH07 Item ME has unknown valid bits")
    if not value.valid & ITEM_ME_VALID_DECISION:
        raise ProtocolError("TH07 Item ME lacks a startup decision")
    if value.item_selftest_failures > value.item_selftest_runs:
        raise ProtocolError("TH07 Item ME failures exceed test runs")
    if value.bullet_retry_passes > value.bullet_retry_runs:
        raise ProtocolError("TH07 Item ME passes exceed retry runs")
    has_failure_detail = bool(value.valid & ITEM_ME_VALID_FAILURE_DETAIL)
    if has_failure_detail != (value.item_selftest_failures != 0):
        raise ProtocolError("TH07 Item ME failure validity is inconsistent")

    if value.state is ItemMeState.UNAVAILABLE:
        if value.reason is not ItemMeReason.ME_UNAVAILABLE:
            raise ProtocolError("unavailable Item ME lacks ME_UNAVAILABLE reason")
    elif value.state is ItemMeState.TESTING:
        if value.reason is not ItemMeReason.NONE:
            raise ProtocolError("testing Item ME already has a final reason")
    elif value.state is ItemMeState.ENABLED:
        if (
            value.reason is not ItemMeReason.SELFTEST_PASS
            or value.item_selftest_runs == 0
            or value.item_selftest_failures != 0
            or value.bullet_retry_runs != 0
            or value.bullet_retry_passes != 0
        ):
            raise ProtocolError("enabled Item ME has an inconsistent decision")
    elif value.state is ItemMeState.SAFE_FALLBACK:
        if (
            value.reason.value < ItemMeReason.LIVE_ACQUIRE.value
            or value.reason.value > ItemMeReason.REJECT_CONTRACT.value
            or value.item_selftest_failures == 0
            or value.bullet_retry_runs == 0
            or value.bullet_retry_passes != value.bullet_retry_runs
        ):
            raise ProtocolError("Item ME safe fallback is inconsistent")
    elif value.state is ItemMeState.FAILED:
        if value.reason is ItemMeReason.BULLET_RETRY_FAILED:
            if (
                value.bullet_retry_runs == 0
                or value.bullet_retry_passes >= value.bullet_retry_runs
            ):
                raise ProtocolError("Item ME failed retry is inconsistent")
        elif value.item_selftest_failures == 0:
            raise ProtocolError("failed Item ME lacks a failed selftest")


def _validate_a1_move(value: A1Move) -> None:
    if value.schema != A1_MOVE_SCHEMA:
        raise ProtocolError(f"unsupported TH07 A1-MOVE schema {value.schema}")
    if value.valid & ~A1_MOVE_VALID_KNOWN:
        raise ProtocolError("TH07 A1-MOVE has unknown valid bits")
    if not value.valid & A1_MOVE_VALID_DECISION:
        raise ProtocolError("TH07 A1-MOVE lacks a startup decision")
    if value.selftest_failures > value.selftest_runs:
        raise ProtocolError("TH07 A1-MOVE failures exceed test runs")
    if value.bullet_retry_passes > value.bullet_retry_runs:
        raise ProtocolError("TH07 A1-MOVE passes exceed retry runs")
    has_failure_detail = bool(value.valid & A1_MOVE_VALID_FAILURE_DETAIL)
    if has_failure_detail != (value.selftest_failures != 0):
        raise ProtocolError("TH07 A1-MOVE failure validity is inconsistent")

    if value.state is ItemMeState.UNAVAILABLE:
        if value.reason not in (
            A1MoveReason.ME_UNAVAILABLE,
            A1MoveReason.ITEM_DRAW_UNAVAILABLE,
        ):
            raise ProtocolError("unavailable A1-MOVE lacks an unavailable reason")
    elif value.state is ItemMeState.TESTING:
        if value.reason is not A1MoveReason.NONE:
            raise ProtocolError("testing A1-MOVE already has a final reason")
    elif value.state is ItemMeState.ENABLED:
        if (
            value.reason is not A1MoveReason.SELFTEST_PASS
            or value.selftest_runs == 0
            or value.selftest_failures != 0
            or value.bullet_retry_runs != 0
            or value.bullet_retry_passes != 0
        ):
            raise ProtocolError("enabled A1-MOVE has an inconsistent decision")
    elif value.state is ItemMeState.SAFE_FALLBACK:
        if (
            value.reason.value < A1MoveReason.BEGIN.value
            or value.reason.value > A1MoveReason.BIT_MISMATCH.value
            or value.selftest_failures == 0
            or value.bullet_retry_runs == 0
            or value.bullet_retry_passes != value.bullet_retry_runs
        ):
            raise ProtocolError("A1-MOVE safe fallback is inconsistent")
    elif value.state is ItemMeState.FAILED:
        if value.reason not in (
            A1MoveReason.COMMON_FATAL,
            A1MoveReason.BULLET_RETRY_FAILED,
        ) or value.selftest_failures == 0:
            raise ProtocolError("failed A1-MOVE has an inconsistent decision")
        if (
            value.reason is A1MoveReason.BULLET_RETRY_FAILED
            and (
                value.bullet_retry_runs == 0
                or value.bullet_retry_passes >= value.bullet_retry_runs
            )
        ):
            raise ProtocolError("A1-MOVE failed retry is inconsistent")


def parse_packet(datagram: bytes) -> Packet:
    if len(datagram) < HEADER_BYTES:
        raise ProtocolError("short header")
    magic, version, type_raw, sequence, uptime, payload_len, flags, build_id = (
        HEADER.unpack_from(datagram)
    )
    if magic != MAGIC:
        raise ProtocolError("bad magic")
    if version != PROTOCOL_VERSION:
        raise ProtocolError(f"unsupported version {version}")
    try:
        packet_type = PacketType(type_raw)
    except ValueError as exc:
        raise ProtocolError(f"unknown packet type {type_raw}") from exc

    if packet_type is PacketType.TH07_STATUS:
        if payload_len not in (STATUS_PAYLOAD_BYTES_V1, STATUS_PAYLOAD_BYTES_V2):
            raise ProtocolError(f"bad payload length {payload_len}")
    elif packet_type is PacketType.TH07_PERF_LOG:
        if not PERF_LOG_FIXED_PAYLOAD_BYTES <= payload_len <= PERF_LOG_MAX_PAYLOAD_BYTES:
            raise ProtocolError(f"bad perf log payload length {payload_len}")
    else:
        expected_payload = {
            PacketType.HELLO: IDENTITY_BYTES,
            PacketType.HEARTBEAT: IDENTITY_BYTES,
            PacketType.SHUTDOWN: IDENTITY_BYTES,
            PacketType.TH07_EVENT: EVENT_PAYLOAD_BYTES,
            PacketType.TH07_PORTRAIT_CACHE: PORTRAIT_CACHE_PAYLOAD_BYTES,
            PacketType.TH07_ITEM_ME: ITEM_ME_PAYLOAD_BYTES,
            PacketType.TH07_A1_MOVE: A1_MOVE_PAYLOAD_BYTES,
        }[packet_type]
        if payload_len != expected_payload:
            raise ProtocolError(f"bad payload length {payload_len}")
    if len(datagram) != HEADER_BYTES + payload_len:
        raise ProtocolError("datagram size does not match header")

    identity = Identity(*IDENTITY.unpack_from(datagram, HEADER_BYTES))
    status: Optional[Status] = None
    event: Optional[Event] = None
    portrait_cache: Optional[PortraitCache] = None
    item_me: Optional[ItemMe] = None
    a1_move: Optional[A1Move] = None
    perf_log: Optional[PerfLogChunk] = None
    body_offset = HEADER_BYTES + IDENTITY_BYTES

    if packet_type is PacketType.TH07_STATUS:
        schema = struct.unpack_from("!H", datagram, body_offset)[0]
        if schema == 1:
            if payload_len != STATUS_PAYLOAD_BYTES_V1:
                raise ProtocolError("TH07 status schema 1 has the wrong size")
            values = list(STATUS_BODY_V1.unpack_from(datagram, body_offset))
        elif schema == 2:
            if payload_len != STATUS_PAYLOAD_BYTES_V2:
                raise ProtocolError("TH07 status schema 2 has the wrong size")
            values = list(STATUS_BODY_V2.unpack_from(datagram, body_offset))
        else:
            raise ProtocolError(f"unsupported TH07 status schema {schema}")
        for index in (4, 5, 22, 27):
            values[index] = _signed(values[index])
        status = Status(*values)
        if status.schema == 1:
            _validate_schema1_status(status)
        else:
            _validate_schema2_status(status)
    elif packet_type is PacketType.TH07_EVENT:
        values = list(EVENT_BODY.unpack_from(datagram, body_offset))
        if values[0] != 1:
            raise ProtocolError(f"unsupported TH07 event schema {values[0]}")
        try:
            values[1] = EventType(values[1])
        except ValueError as exc:
            raise ProtocolError(f"unknown TH07 event {values[1]}") from exc
        for index in (3, 4, 6, 7, 8):
            values[index] = _signed(values[index])
        event = Event(*values)
    elif packet_type is PacketType.TH07_PORTRAIT_CACHE:
        values = PORTRAIT_CACHE_BODY.unpack_from(datagram, body_offset)
        if values[0] != PORTRAIT_CACHE_SCHEMA:
            raise ProtocolError(
                f"unsupported TH07 portrait cache schema {values[0]}"
            )
        slots = tuple(
            PortraitSlot(*values[19 + index * 11 : 30 + index * 11])
            for index in range(PORTRAIT_SLOT_COUNT)
        )
        portrait_cache = PortraitCache(*values[:19], slots=slots)
        _validate_portrait_cache_wire(portrait_cache)
    elif packet_type is PacketType.TH07_PERF_LOG:
        values = PERF_LOG_BODY.unpack_from(datagram, body_offset)
        if values[-1] != 0:
            raise ProtocolError("TH07 perf log reserved field is nonzero")
        data = datagram[HEADER_BYTES + PERF_LOG_FIXED_PAYLOAD_BYTES :]
        perf_log = PerfLogChunk(*values[:-1], data=data)
        _validate_perf_log_chunk(perf_log)
    elif packet_type is PacketType.TH07_ITEM_ME:
        values = list(ITEM_ME_BODY.unpack_from(datagram, body_offset))
        if values[0] != ITEM_ME_SCHEMA:
            raise ProtocolError(f"unsupported TH07 Item ME schema {values[0]}")
        try:
            values[2] = ItemMeState(values[2])
        except ValueError as exc:
            raise ProtocolError(f"unknown TH07 Item ME state {values[2]}") from exc
        try:
            values[3] = ItemMeReason(values[3])
        except ValueError as exc:
            raise ProtocolError(f"unknown TH07 Item ME reason {values[3]}") from exc
        values[8] = _signed(values[8])
        item_me = ItemMe(*values)
        _validate_item_me(item_me)
    elif packet_type is PacketType.TH07_A1_MOVE:
        values = list(A1_MOVE_BODY.unpack_from(datagram, body_offset))
        if values[0] != A1_MOVE_SCHEMA:
            raise ProtocolError(f"unsupported TH07 A1-MOVE schema {values[0]}")
        try:
            values[2] = ItemMeState(values[2])
        except ValueError as exc:
            raise ProtocolError(f"unknown TH07 A1-MOVE state {values[2]}") from exc
        try:
            values[3] = A1MoveReason(values[3])
        except ValueError as exc:
            raise ProtocolError(f"unknown TH07 A1-MOVE reason {values[3]}") from exc
        values[8] = _signed(values[8])
        a1_move = A1Move(*values)
        _validate_a1_move(a1_move)

    return Packet(
        packet_type=packet_type,
        sequence=sequence,
        uptime_ms=uptime,
        flags=flags,
        build_id=build_id,
        identity=identity,
        status=status,
        event=event,
        portrait_cache=portrait_cache,
        item_me=item_me,
        a1_move=a1_move,
        perf_log=perf_log,
    )


@dataclass
class _PerfLogAssembly:
    chunk: PerfLogChunk
    identity: Identity
    source: tuple[str, int]
    build_id: int
    chunks: dict[int, bytes] = field(default_factory=dict)
    saw_begin: bool = False
    saw_end: bool = False
    duplicates: int = 0
    last_seen: float = field(default_factory=time.monotonic)


class PerfLogReassembler:
    """Reassemble type-11 datagrams; publish only CRC-complete snapshots."""

    MAX_PENDING = 8
    MAX_REMEMBERED = 64

    def __init__(self, output_base: Optional[Path]) -> None:
        self.output_base = output_base
        self.pending: dict[tuple[str, int, int, int, int], _PerfLogAssembly] = {}
        self.completed: dict[tuple[str, int, int, int, int], float] = {}
        self.poisoned: dict[tuple[str, int, int, int, int], float] = {}

    @classmethod
    def _remember(
        cls,
        collection: dict[tuple[str, int, int, int, int], float],
        key: tuple[str, int, int, int, int],
        now: float,
    ) -> None:
        collection[key] = now
        while len(collection) > cls.MAX_REMEMBERED:
            del collection[next(iter(collection))]

    @staticmethod
    def _key(
        packet: Packet, source: tuple[str, int]
    ) -> tuple[str, int, int, int, int]:
        assert packet.perf_log
        value = packet.perf_log
        return (
            source[0],
            source[1],
            packet.build_id,
            value.run_id,
            value.snapshot_id,
        )

    @staticmethod
    def _same_metadata(
        assembly: _PerfLogAssembly, packet: Packet
    ) -> bool:
        assert packet.perf_log
        first = assembly.chunk
        value = packet.perf_log
        return (
            assembly.identity == packet.identity
            and first.run_id == value.run_id
            and first.snapshot_id == value.snapshot_id
            and first.window_id == value.window_id
            and first.total_bytes == value.total_bytes
            and first.log_crc32 == value.log_crc32
            and first.dropped_lines == value.dropped_lines
            and first.chunk_count == value.chunk_count
            and bool(first.flags & PERF_LOG_VALID)
            == bool(value.flags & PERF_LOG_VALID)
        )

    @staticmethod
    def _incomplete_line(
        assembly: _PerfLogAssembly, reason: str
    ) -> str:
        value = assembly.chunk
        missing = value.chunk_count - len(assembly.chunks)
        return (
            f"[TH07 PERF LOG INCOMPLETE] RID={value.run_id:08X} "
            f"SNAP={value.snapshot_id} W={value.window_id} "
            f"TOTAL={value.total_bytes} CRC=0x{value.log_crc32:08X} "
            f"HAVE={len(assembly.chunks)}/{value.chunk_count} "
            f"MISSING={missing} DUP={assembly.duplicates} REASON={reason}"
        )

    def _confirmed_path(self, value: PerfLogChunk) -> Optional[Path]:
        if self.output_base is None:
            return None
        suffix = self.output_base.suffix or ".LOG"
        stem = self.output_base.stem if self.output_base.suffix else self.output_base.name
        return self.output_base.with_name(
            f"{stem}.RID{value.run_id:08X}.SNAP{value.snapshot_id}."
            f"TOTAL{value.total_bytes}.CRC{value.log_crc32:08X}{suffix}"
        )

    def accept(
        self, packet: Packet, source: tuple[str, int], now: Optional[float] = None
    ) -> list[str]:
        value = packet.perf_log
        if value is None:
            return []
        current_time = time.monotonic() if now is None else now
        key = self._key(packet, source)
        if key in self.completed:
            return []
        if key in self.poisoned:
            return []

        lines: list[str] = []
        session = key[:4]
        for old_key, old in tuple(self.pending.items()):
            if old_key[:4] == session and old_key != key:
                lines.append(self._incomplete_line(old, "superseded"))
                del self.pending[old_key]

        assembly = self.pending.get(key)
        if assembly is None:
            if len(self.pending) >= self.MAX_PENDING:
                oldest_key = min(
                    self.pending, key=lambda pending_key: self.pending[pending_key].last_seen
                )
                oldest = self.pending.pop(oldest_key)
                lines.append(self._incomplete_line(oldest, "pending-capacity"))
            assembly = _PerfLogAssembly(
                chunk=value,
                identity=packet.identity,
                source=source,
                build_id=packet.build_id,
                last_seen=current_time,
            )
            self.pending[key] = assembly
            lines.append(
                f"[TH07 PERF LOG START] RID={value.run_id:08X} "
                f"SNAP={value.snapshot_id} W={value.window_id} "
                f"TOTAL={value.total_bytes} CRC=0x{value.log_crc32:08X} "
                f"CHUNKS={value.chunk_count}"
            )
        elif not self._same_metadata(assembly, packet):
            del self.pending[key]
            self._remember(self.poisoned, key, current_time)
            raise ProtocolError("TH07 perf log snapshot metadata changed")

        assembly.last_seen = current_time
        previous = assembly.chunks.get(value.chunk_index)
        if previous is not None:
            if previous != value.data:
                del self.pending[key]
                self._remember(self.poisoned, key, current_time)
                raise ProtocolError("TH07 perf log duplicate chunk conflicts")
            assembly.duplicates += 1
        else:
            assembly.chunks[value.chunk_index] = value.data
        assembly.saw_begin |= bool(value.flags & PERF_LOG_BEGIN)
        assembly.saw_end |= bool(value.flags & PERF_LOG_END)

        if (
            len(assembly.chunks) != value.chunk_count
            or not assembly.saw_begin
            or not assembly.saw_end
        ):
            return lines

        data = b"".join(
            assembly.chunks[index] for index in range(value.chunk_count)
        )
        actual_crc = zlib.crc32(data) & 0xFFFF_FFFF
        if len(data) != value.total_bytes or actual_crc != value.log_crc32:
            lines.append(self._incomplete_line(assembly, "whole-log-crc"))
            del self.pending[key]
            self._remember(self.poisoned, key, current_time)
            return lines

        confirmed_path = self._confirmed_path(value)
        if confirmed_path is not None:
            confirmed_path.parent.mkdir(parents=True, exist_ok=True)
            temporary = confirmed_path.with_name(confirmed_path.name + ".tmp")
            temporary.write_bytes(data)
            temporary.replace(confirmed_path)
        profile = "VALID" if value.flags & PERF_LOG_VALID else "INVALID"
        path_text = str(confirmed_path) if confirmed_path is not None else "OFF"
        lines.append(
            f"[TH07 PERF LOG COMPLETE] RID={value.run_id:08X} "
            f"SNAP={value.snapshot_id} W={value.window_id} "
            f"TOTAL={value.total_bytes} CRC=0x{actual_crc:08X} "
            f"PROFILE={profile} DROP={value.dropped_lines} "
            f"DUP={assembly.duplicates} FILE={path_text}"
        )
        del self.pending[key]
        self._remember(self.completed, key, current_time)
        return lines

    def expire(self, now: float, max_age: float, reason: str) -> list[str]:
        lines: list[str] = []
        for key, assembly in tuple(self.pending.items()):
            if now - assembly.last_seen >= max_age:
                lines.append(self._incomplete_line(assembly, reason))
                del self.pending[key]
        return lines

    def finish(self, reason: str) -> list[str]:
        lines = [
            self._incomplete_line(assembly, reason)
            for assembly in self.pending.values()
        ]
        self.pending.clear()
        return lines


class Logger:
    def __init__(self, path: Optional[Path], stream: TextIO = sys.stdout) -> None:
        self.stream = stream
        self.file = path.open("a", encoding="utf-8", buffering=1) if path else None

    def emit(self, message: str) -> None:
        now = dt.datetime.now().astimezone().isoformat(timespec="milliseconds")
        line = f"{now} {message}"
        print(line, file=self.stream, flush=True)
        if self.file:
            print(line, file=self.file, flush=True)

    def close(self) -> None:
        if self.file:
            self.file.close()
            self.file = None


def _kib(value: int) -> int:
    return value // 1024


def _flag_words(status: Status) -> str:
    pairs = (
        (STATUS_BGM_PLAYING, "BGM"),
        (STATUS_BGM_PAUSED, "BGM_PAUSED"),
        (STATUS_DEMO, "DEMO"),
        (STATUS_REPLAY, "REPLAY"),
        (STATUS_GAME_PAUSED, "PAUSED"),
        (STATUS_BOSS, "BOSS"),
        (STATUS_SPELL, "SPELL"),
        (STATUS_DIALOGUE, "DIALOGUE"),
        (STATUS_FATAL_SEEN, "FATAL"),
        (STATUS_AUDIO4M_PROVEN, "AUDIO4M_PROVEN"),
        (STATUS_GE_UPPER_PORTRAIT, "GE_PORTRAIT"),
        (STATUS_SFX_MAIN_RAM, "SFX_MAINRAM"),
        (STATUS_BGM_MAIN_RAM, "BGM_MAINRAM"),
    )
    words = [word for bit, word in pairs if status.flags & bit]
    return ",".join(words) if words else "IDLE"


def _me_stream_result(value: int) -> str:
    names = {
        0: "OK",
        1: "VERSION",
        2: "BOUNDS",
        3: "PROTOCOL",
        4: "INPUT-HASH",
        5: "RECORD",
        6: "OUTPUT-OVERFLOW",
        7: "RUN-OVERFLOW",
        0xFFFF_FFFF: "N/A",
    }
    return names.get(value, f"0x{value:08X}")


def _bullet_compact_result(value: int) -> str:
    names = {
        0: "OK",
        1: "VERSION",
        2: "BOUNDS",
        3: "SEED",
        4: "RECORD",
        5: "PROTOCOL",
        6: "GUARD",
        0xFFFF_FFFF: "N/A",
    }
    return names.get(value, f"0x{value:08X}")


def _item_motion_result(value: int) -> str:
    names = {
        0: "OK",
        1: "DISABLED",
        2: "VERSION",
        3: "SEED",
        4: "RECORD",
        5: "GUARD",
        6: "BOUNDS",
        7: "PROTOCOL",
        0xFFFF_FFFF: "N/A",
    }
    return names.get(value, f"0x{value:08X}")


def format_packet(packet: Packet, source: tuple[str, int]) -> list[str]:
    prefix = (
        f"peer={source[0]}:{source[1]} model={packet.identity.model} "
        f"profile={packet.identity.profile} local={packet.identity.local_ip} "
        f"build=0x{packet.build_id:08X} seq={packet.sequence} "
        f"up={packet.uptime_ms}ms"
    )
    if packet.packet_type is PacketType.HELLO:
        return [f"[SHIKIGAMI PSP ONLINE] {prefix}"]
    if packet.packet_type is PacketType.HEARTBEAT:
        return [f"[HEARTBEAT] {prefix}"]
    if packet.packet_type is PacketType.SHUTDOWN:
        return [f"[SHIKIGAMI PSP SHUTDOWN] {prefix}"]
    if packet.perf_log:
        # Chunk progress is aggregated by PerfLogReassembler; printing every
        # datagram would bury STATUS/EVENT output under hundreds of lines.
        return []
    if packet.a1_move:
        value = packet.a1_move
        bullet_on = value.state in (
            ItemMeState.ENABLED,
            ItemMeState.SAFE_FALLBACK,
        ) or (
            value.state is ItemMeState.UNAVAILABLE
            and value.reason is A1MoveReason.ITEM_DRAW_UNAVAILABLE
        )
        mismatch = (
            "N/A"
            if value.first_mismatch_slot == 0xFFFF_FFFF
            else str(value.first_mismatch_slot)
        )
        return [
            f"[TH07 A1 MOVE] A1_MOVE={value.state.name.replace('_', '-')} "
            f"ITEM_MOVE={'ON' if value.state is ItemMeState.ENABLED else 'OFF'} "
            f"BULLET_ME={'ON' if bullet_on else 'OFF'} "
            f"REASON={value.reason.name.replace('_', '-')} "
            f"TEST={value.selftest_runs}/{value.selftest_failures} "
            f"RETRY={value.bullet_retry_runs}/{value.bullet_retry_passes} "
            f"POLL={value.last_poll_result} "
            f"BULLET={_bullet_compact_result(value.last_bullet_result)} "
            f"ITEM={_item_motion_result(value.last_item_result)} "
            f"MISMATCH={mismatch} {prefix}"
        ]
    if packet.item_me:
        value = packet.item_me
        bullet_on = value.state is ItemMeState.ENABLED or (
            value.state is ItemMeState.SAFE_FALLBACK
            and value.bullet_retry_runs != 0
            and value.bullet_retry_passes == value.bullet_retry_runs
        )
        return [
            f"[TH07 ITEM ME] ITEM_ME={value.state.name.replace('_', '-')} "
            f"BULLET_ME={'ON' if bullet_on else 'OFF'} "
            f"REASON={value.reason.name.replace('_', '-')} "
            f"TEST={value.item_selftest_runs}/{value.item_selftest_failures} "
            f"RETRY={value.bullet_retry_runs}/{value.bullet_retry_passes} "
            f"WAIT={value.last_wait_result} "
            f"STREAM={_me_stream_result(value.last_stream_result)} "
            f"ITEM={_me_stream_result(value.last_item_result)} {prefix}"
        ]
    if packet.portrait_cache:
        value = packet.portrait_cache
        assessment, reasons = _portrait_cache_assessment(value, packet.identity)
        bridge = (
            "ACTIVE" if value.bridge_active else "INACTIVE"
        ) + ("/LOCKED" if value.power_lock_held else "/UNLOCKED")
        draws = ",".join(str(slot.draw_count) for slot in value.slots)
        roles = ",".join(
            PORTRAIT_ROLE_NAMES.get(slot.role, f"ROLE{slot.role}")
            for slot in value.slots
        )
        prewarm = "COMPLETE" if value.required_mask else "PENDING"
        required_count = value.required_mask.bit_count()
        owned_count = value.owned_mask.bit_count()
        verified_count = value.verified_mask.bit_count()
        verified_hashes = sum(
            bool(value.required_mask & (1 << index))
            and slot.source_hash != 0
            and slot.source_hash == slot.readback_hash
            for index, slot in enumerate(value.slots)
        )
        hash_total = str(required_count) if value.required_mask else "PENDING"
        reason = "NONE" if not reasons else " | ".join(reasons)
        return [
            f"[TH07 PORTRAIT CACHE] PORTRAIT CACHE={assessment} "
            f"STAGE={value.stage} PREWARM={prewarm} BRIDGE={bridge} "
            f"APERTURE={_kib(value.live_aperture_bytes)}KiB "
            f"POOL={_kib(value.pool_bytes)}KiB@0x{value.pool_raw_base:08X} "
            f"FLAGS=0x{value.flags:02X} GEN={value.cache_generation} "
            f"MASK=R0x{value.required_mask:02X}/O0x{value.owned_mask:02X}/"
            f"V0x{value.verified_mask:02X} SAMPLED=0x{value.sampled_mask:02X} "
            f"COUNT=R{required_count}/O{owned_count}/V{verified_count} "
            f"LIVE={_kib(value.live_bytes)}KiB "
            f"HASH={verified_hashes}/{hash_total} "
            f"ROLE=[{roles}] DRAW=[{draws}] "
            f"FAULT=F{value.fallback_count}/M{value.migration_count}/"
            f"A{value.allocation_failure_count}/I{value.invariant_failure_count} "
            f"REASON={reason} {prefix}"
        ]
    if packet.status:
        value = packet.status
        fps = f"{value.fps_x10 / 10:.1f}" if value.valid & VALID_FPS else "N/A"
        if value.valid & VALID_GE_UPPER_OWNED:
            ge_prior = (
                f"{_kib(value.ge_prior_bytes)}KiB@0x{value.ge_prior_base:08X} "
                "OWNED/PORTRAIT"
            )
        elif value.valid & VALID_GE_PRIOR_EVIDENCE:
            ge_prior = (
                f"{_kib(value.ge_prior_bytes)}KiB@0x{value.ge_prior_base:08X} "
                "PRIOR/NO-OWNER"
            )
        else:
            ge_prior = "UNKNOWN"
        bgm_main_ram = bool(value.flags & STATUS_BGM_MAIN_RAM)
        if bgm_main_ram:
            me_ring = "UNUSED"
        elif value.valid & VALID_ME_UPPER_OWNED:
            me_region = (
                "LOWER"
                if value.me_upper_base + value.me_upper_bytes <= 0x00200000
                else "UPPER"
            )
            me_ring = (
                f"{_kib(value.me_upper_bytes)}KiB@0x{value.me_upper_base:08X}/"
                f"{me_region}"
            )
        else:
            me_ring = "0KiB UNKNOWN"
        me_perf = (
            f"JOBS={value.me_jobs} FALLBACK={value.me_fallbacks} "
            f"TIMEOUT={value.me_timeouts} MAXWAIT={value.me_max_wait_us}us"
            if value.valid & VALID_ME_PERF_WINDOW
            else "N/A"
        )
        ring_frames = value.bgm_ring_bytes // 4
        audio4m = ""
        if value.schema == 2:
            proof_count = bin(value.audio4m_proof_flags).count("1")
            sfx_main_ram = bool(value.flags & STATUS_SFX_MAIN_RAM)
            proof_total = 5 if sfx_main_ram else 8
            if value.flags & STATUS_AUDIO4M_PROVEN:
                proof_state = "PROVEN-SE-MAINRAM" if sfx_main_ram else "PROVEN"
            elif value.sfx_coverage_active or value.sfx_coverage_complete:
                proof_state = f"DIAGNOSTIC-{proof_count}/{proof_total}"
            elif _audio4m_runtime_active(value):
                proof_state = (
                    "ACTIVE-BGM-MAINRAM"
                    if bgm_main_ram
                    else "ACTIVE-BGM384K-SE-MAINRAM"
                    if sfx_main_ram
                    else "ACTIVE-4MiB-IN-USE"
                )
            else:
                proof_state = f"READY-{proof_count}/{proof_total}"
            audio4m = (
                f" AUDIO4M={proof_state}/0x{value.audio4m_proof_flags:02X} "
                f"SFX={value.sfx_atlas_bytes} "
                f"CAN={value.sfx_canonical_bytes}/0x{value.sfx_canonical_output_mask:08X} "
                f"REP={value.sfx_replica_bytes}/0x{value.sfx_replica_output_mask:08X} "
                f"COV=A{value.sfx_coverage_active}C{value.sfx_coverage_complete}"
                f"P{value.sfx_coverage_pass}@{value.sfx_coverage_buffer} "
                f"SFXJOB={value.sfx_mix_jobs}/{value.sfx_output_blocks} "
                f"FIFO={value.sfx_fifo_misses} SFXFATAL={value.sfx_fatal} "
                f"WRAP={value.bgm_upload_wraps}/{value.bgm_fetch_wraps}/"
                f"{value.bgm_output_wraps}"
            )
        return [
            f"[TH07 STATUS] FPS={fps} STATE={value.supervisor_state} "
            f"STAGE={value.stage} SPELL={value.spell_index} FLAGS={_flag_words(value)} "
            f"MAIN_FREE={_kib(value.main_free)}KiB MAX={_kib(value.main_maxfree)}KiB "
            f"GE_UPPER={ge_prior} ME_EDRAM={me_ring} "
            f"ME_PERF={me_perf} "
            f"BGM_RING={value.bgm_ring_bytes}"
            f"{'/MAIN' if bgm_main_ram else '/ME'} "
            f"FILL={value.bgm_fill_frames}/{ring_frames} "
            f"UND={value.underruns} BGMIDX={value.bgm_index} "
            f"FATAL={value.fatal_count}/0x{value.fatal_hash:08X}"
            f"{audio4m} {prefix}"
        ]
    assert packet.event is not None
    value = packet.event
    return [
        f"[TH07 EVENT {value.event_type.name}] #{value.event_count} "
        f"{value.old_value}->{value.new_value} frame={value.frame} "
        f"state={value.supervisor_state} stage={value.stage} bgm={value.bgm_index} "
        f"und={value.underruns} fatal=0x{value.fatal_hash:08X} {prefix}"
    ]


def run_receiver(
    bind: str,
    port: int,
    log_path: Optional[Path],
    lost_after: float,
    perf_log_path: Optional[Path] = Path(DEFAULT_PERF_LOG),
) -> int:
    logger = Logger(log_path)
    perf_logs = PerfLogReassembler(perf_log_path)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind, port))
    sock.settimeout(0.25)
    logger.emit(f"[RECEIVER READY] udp://{bind}:{port} log={log_path or 'OFF'}")
    last_seen: Optional[float] = None
    online = False
    try:
        while True:
            try:
                datagram, source = sock.recvfrom(MAX_DATAGRAM_BYTES)
            except socket.timeout:
                now = time.monotonic()
                for line in perf_logs.expire(now, lost_after, "packet-timeout"):
                    logger.emit(line)
                if online and last_seen is not None and now - last_seen > lost_after:
                    logger.emit("[SHIKIGAMI PSP LOST] heartbeat timeout")
                    online = False
                continue
            try:
                packet = parse_packet(datagram)
            except ProtocolError as exc:
                logger.emit(f"[DROP] peer={source[0]}:{source[1]} {exc}")
                continue
            last_seen = time.monotonic()
            if not online:
                logger.emit(f"[SHIKIGAMI PSP ONLINE] peer={source[0]}:{source[1]}")
                online = True
            try:
                for line in perf_logs.accept(packet, source):
                    logger.emit(line)
            except (ProtocolError, OSError) as exc:
                logger.emit(
                    f"[TH07 PERF LOG INCOMPLETE] peer={source[0]}:{source[1]} "
                    f"REASON={exc}"
                )
            for line in format_packet(packet, source):
                logger.emit(line)
            if packet.packet_type is PacketType.SHUTDOWN:
                for line in perf_logs.finish("psp-shutdown"):
                    logger.emit(line)
                online = False
    except KeyboardInterrupt:
        for line in perf_logs.finish("receiver-stop"):
            logger.emit(line)
        logger.emit("[RECEIVER STOPPED]")
        return 0
    finally:
        sock.close()
        logger.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default=DEFAULT_BIND)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--log", type=Path, default=Path(DEFAULT_LOG))
    parser.add_argument(
        "--perf-log",
        type=Path,
        default=Path(DEFAULT_PERF_LOG),
        help="base name for CRC-confirmed RAM-log snapshots",
    )
    parser.add_argument("--lost-after", type=float, default=DEFAULT_LOST_AFTER)
    args = parser.parse_args()
    return run_receiver(
        args.bind, args.port, args.log, args.lost_after, args.perf_log
    )


if __name__ == "__main__":
    raise SystemExit(main())

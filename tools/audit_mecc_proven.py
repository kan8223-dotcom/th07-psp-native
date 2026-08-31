#!/usr/bin/env python3
"""Verify that a rebuilt MECC archive matches the real-hardware-proven payload.

GNU ar in this PSP toolchain records archive timestamps, so the whole-archive
SHA changes on a clean rebuild.  The six member payloads and embedded kcall PRX
are deterministic; those are the bytes the linker consumes and the PSP loads.
"""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path


PROVEN_ARCHIVE_SHA256 = (
    "34e2cc9b5975367da8e9c7987e251d3564241e9f2d641d0c7098b970e71d68fa"
)
PROVEN_KCALL_SHA256 = (
    "3f35bdcea388a5d9a86b672282a35d5a66d8f4ef27fc36190bd07f2a126bab93"
)
PROVEN_MEMBERS = {
    "me-lib.c.obj": "7a49ec9fcfe212a69c2627779aed35011d3779a44ff8670c83d561165cf02417",
    "me-core-custom.c.obj": "bd4335bab40eb7cbc0a08ac00508940a7fa37130e61ed964c16b71fb21ab3ce3",
    "me-core-mapping.c.obj": "b46da815e52ea62b8843e6636fec954b1c3fe7f5bc89e87cfa2463abf976f44d",
    "context.S.obj": "1abb332227fcc9cda7ea04fc54b58c2f2f76e81e75aecd5a8eb288a51bc7e699",
    "embedded.c.obj": "e85d92a83c6e2dbaf65ad45b1d00f2b6786aca77162eda4a7c79d8a14d9cca56",
    "kcall.S.obj": "741b7f4bf875b58827ea6fce78bd1004e5d7c2bbcc1e7891288a264f32cb20e2",
}
# RID22 used this object for the hardware-accepted Bullet worker and usage
# meter.  Its full-cache routine remains semantically under investigation for
# live Item handoff, but this is the only render-worker object accepted for
# playable builds.
RENDER_WORKER_RID22_PROVEN_ME_LIB_SHA256 = (
    "9c50aa7af6d22dc00abe42830fb2d85b9c1da29364d127016bd3fc02b40462a3"
)
# RID27 is retained as rejected provenance.  Its index+8192 change produced
# the exact same stale IR result on hardware and must never pass the audit.
RENDER_WORKER_RID27_REJECTED_ME_LIB_SHA256 = (
    "5c764f176ef82387d3cb706fec9ecdaae9d65a83e5843e713ff39e495bbea3ee"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_ar(*args: str) -> bytes:
    return subprocess.run(
        ["psp-ar", *args], check=True, stdout=subprocess.PIPE
    ).stdout


def main() -> int:
    if len(sys.argv) not in (3, 4) or (
        len(sys.argv) == 4 and sys.argv[3] != "--render-worker-candidate"
    ):
        print(
            f"usage: {Path(sys.argv[0]).name} LIBME_CORE_A KCALL_PRX "
            "[--render-worker-candidate]",
            file=sys.stderr,
        )
        return 2

    archive = Path(sys.argv[1])
    kcall = Path(sys.argv[2])
    if not archive.is_file() or not kcall.is_file():
        print("MECC audit input is missing", file=sys.stderr)
        return 1

    render_worker_candidate = len(sys.argv) == 4
    expected_members = dict(PROVEN_MEMBERS)
    if render_worker_candidate:
        expected_members["me-lib.c.obj"] = (
            RENDER_WORKER_RID22_PROVEN_ME_LIB_SHA256
        )

    actual_members = run_ar("t", str(archive)).decode("utf-8").splitlines()
    if actual_members != list(expected_members):
        print(f"MECC archive members changed: {actual_members!r}", file=sys.stderr)
        return 1
    for member, expected in expected_members.items():
        actual = sha256(run_ar("p", str(archive), member))
        if actual != expected:
            print(
                f"MECC member mismatch {member}: expected {expected}, got {actual}",
                file=sys.stderr,
            )
            return 1

    actual_kcall = sha256(kcall.read_bytes())
    if actual_kcall != PROVEN_KCALL_SHA256:
        print(
            f"MECC kcall mismatch: expected {PROVEN_KCALL_SHA256}, got {actual_kcall}",
            file=sys.stderr,
        )
        return 1

    if render_worker_candidate:
        print(
            "MECC RID22 render-worker payload: OK "
            "(Item live-handoff remains disabled; all other members "
            f"and kcall match proven payload {PROVEN_KCALL_SHA256})"
        )
    else:
        print(
            "MECC proven payload: OK "
            f"(frozen archive {PROVEN_ARCHIVE_SHA256}; member-equivalent rebuild; "
            f"kcall {PROVEN_KCALL_SHA256})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

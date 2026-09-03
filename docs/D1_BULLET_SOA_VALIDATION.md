# D1 Bullet hot-state SoA: implementation and validation contract

Status: the current D1 queue is A6v4W-based and all hardware gates remain
pending. This document does not authorize a Memory Stick deployment or a
PSP-1000 build. The immutable comparison baseline is the hardware-proven
`RID30-A6V4W-CP932-WAVE` EBOOT (SHA-256
`DAF87978883A918597452B13510B7810D7CA9D931F7FFDF4E0BD02BCD3427B92`).
It fixes `TITLE_WORKSPACE=1`, `TITLE_TRANSIENT=0`,
`TITLE_FONT_HOLE_SWAP=1`, and `LOCAL_FONT_SUBSET=1`; CP932 `0x8160` is the
Windows/MS Gothic `U+FF5E` mapping. The older RID30 D1 targets/artifacts are
retained as history, not used as the A6v4W comparison queue.

## Purpose

The PSP-3000 `Bullet` object is 3,452 bytes and the canonical update walks up
to 1,024 stable slots.  D1 attacks the resulting scattered-read cost.  It does
not reduce bullet count, change collision, change floating-point order, or
move gameplay commits away from SC.

The first implementation reuses the already-banked I-ME7 next-frame seed as
the hot-state shadow.  Its fields use the same raw IEEE-754 words and meanings
as the existing seed ABI, but are stored as field planes.  This makes the
layout a real structure of arrays while retaining the proven generation,
manager-mutation-epoch and frame-identity fences.

## Reversible 2x2 profile matrix

The A6v4W baseline and the three D1 profiles form a 2x2 comparison. Seed
layout and trusted-reader authority are independent switches; neither may be
silently inherited from another research profile.

| Profile | Seed layout | Trusted reader | Purpose |
| --- | --- | --- | --- |
| A6v4W | BS11 AoS | OFF | Hardware-proven immutable baseline |
| D1S0 | BS11 AoS | ON | `psp3000-a6v4w-me-d1s0-trusted-build` |
| D1A | BS13 SoA | OFF | `psp3000-a6v4w-me-d1a-soa-build` |
| D1B | BS13 SoA | ON | `psp3000-a6v4w-me-d1b-soa-build` |

**D1S0** must prove the existing trusted-seed path independently before D1B
is eligible.  It accepts only captured `BULLET_NORMAL` records whose command
cursor is idle, `exFlags == 0`, `spawnDelay == 0`, full 32-bit generation
matches, and manager mutation identity is unchanged.  Every other slot must
immediately use the canonical AoS path.  D1S0 is a correctness gate, not a
performance candidate.

**D1A** publishes and consumes the transposed SoA seed while leaving canonical
AoS revalidation enabled.  It is also a correctness-only gate and is expected
to perform the same or worse because it still pays both representations.

**D1B** combines the D1A layout with the D1S0 trusted-reader path.  Performance
claims are forbidden for RID30 rebuilds, D1S0 and D1A; only D1B may claim a D1
performance result against the immutable RID30 artifact, and only with PSP
`sceKernelGetSystemTimeWide()` HWFPS/ELUS data.  Replay `curFps`, SHIKIGAMI
legacy FPS and PC replay speed are invalid performance inputs.

## Layout contract

The SoA field planes are, in order:

- generation
- current position X/Y/Z raw words
- velocity X/Y/Z raw words
- sprite width/height raw words
- graze width/height raw words
- next position X/Y/Z raw words

Candidate and in-bounds values remain 1,024-bit planes.  Slot identity is the
array index.  Each of the fourteen raw-u32 field planes has 1,024 logical slot
words followed by 16 padding words: its physical stride is therefore 1,040
u32, or 4,160 bytes.  Slots 0 through 1,023 are the only valid records; the
padding is reserved, is not a candidate, and must never be consumed as bullet
state.

The metadata prefix is 320 bytes, so one complete seed bank is
`320 + 14 * 4,160 = 58,560` bytes.  This is distinct from both the rejected
57,664-byte dense-4-KiB SoA draft and the 65,728-byte legacy BS11 AoS transfer
measured by RID25.

A dense 1,024-u32 plane would be exactly 4 KiB.  Accessing the same slot in
several such planes would preserve the low address bits and place every plane
on the same repeating cache-set index, creating a pathological set-conflict
pattern in the hot multi-plane reader.  The additional 16 u32 are one 64-byte
cache line, so a 4,160-byte stride rotates successive plane bases by one cache
line instead of repeating at the 4-KiB boundary.  This padding is an ABI and
cache-ownership property: size/offset assertions, payload bounds, hashes and
writeback/invalidate ranges must all use the physical 4,160-byte stride.

The SoA ABI has a unique protocol version and cannot be mixed with the old AoS
seed or C2b seed.  The whole arena and every plane base remain cache-line
aligned.

This ordering deliberately matches the old ME seed record semantics.  Later
authoritative cutover can hand plane slices to ME without another semantic
translation.  No new per-frame AoS-to-SoA traversal is allowed: publication
must remain a by-product of the already-paid render/update walk.

## Authority and fail-closed rules

- AoS remains canonical for spawn/despawn, commands, ECL mutation, ANM,
  collision-positive paths, graze, score/PIV, Item generation and SFX.
- Slot generation, stage/manager/frame identity and manager mutation epoch
  must all match before a SoA result is read.
- A missing candidate, malformed plane, stale generation, late ME result,
  unsupported state, OOM or protocol mismatch falls back for that frame/slot;
  it never waits and never partially commits.
- Reverse slot update order, six collision buckets and linked-list draw order
  remain byte-for-byte canonical.
- C1 UV16/XYZ16, all C2 slim ABIs, Effect ME and lean-cache ownership remain
  OFF in the initial D1 profiles.
- PSP-1000 is compile-time excluded and is not rebuilt for this increment.

## PC gates

1. Compile the shared header in RID30, D1S0, D1A and D1B configurations; assert
   exact size, alignment, 4,160-byte plane strides, offsets, 1,024-slot logical
   capacity and unique protocol version.
2. Run an independent transpose oracle over 0/1/128/512/1024 slots, including
   sparse words, slot 1023, signed zero and finite edge values.  Every logical
   field must match the legacy seed bit-for-bit.
3. In D1S0, prove generation/mutation/frame mismatch and every unsupported
   state reject before any motion write.  D1B may not advance if this trusted
   AoS correctness gate fails.
4. Run existing compact-update, trusted-authority, collision-boundary,
   Item-motion and ME render correctness suites.
5. Build PSP-3000 A6v4W, D1S0, D1A and D1B. The A6v4W reference is the SHA
   above; any title/font or unrelated feature delta is a blocker.
6. Audit the final ELF/ME object for the selected SoA plane strides, unique
   protocol constants, cache extents, and absence of C1/C2/Effect defines.

## Build isolation

The A6v4W baseline, D1S0, D1A and D1B share `TH07PSP.elf`, `EBOOT.PBP`, object
files and profile-stamp paths, and their recipes clean those shared outputs. The profile
targets must never be requested in parallel or under `make -j`.  Build exactly
one profile at a time, archive its ELF/EBOOT, build ID, profile tuple and hashes,
and only then clean and start the next profile.

The older RID30 artifacts remain historical, non-deploy records:

- `build/final60/rid30_d1s0_trusted_aos_v2_pc_20260901/`
- `build/final60/rid30_d1a_soa_skew_v2_pc_20260901/`
- `build/final60/rid30_d1b_soa_trusted_v2_pc_20260901/`

The current A6v4W queue was built serially and frozen after 759 PC tests passed.
The A6v4W reference rebuilt byte-for-byte as the accepted EBOOT before the
three D1 profiles were produced.  Current artifacts are:

- A6v4W reference:
  `build/final60/rid30_a6v4w_cp932_wave_dash_pc_20260901/`, EBOOT
  `DAF87978883A918597452B13510B7810D7CA9D931F7FFDF4E0BD02BCD3427B92`
- D1S0:
  `build/final60/a6v4w_d1s0_trusted_aos_pc_20260901/`, EBOOT
  `A42A643C8D36DCC5F015E34136BFBDB1CC40E57955BDEA4A924EA72857C82898`
- D1A:
  `build/final60/a6v4w_d1a_soa_shadow_pc_20260901/`, EBOOT
  `29AD853CAA4FE15B67ADB29C090922F644C4FE28B39F54E63C0A7897EA8A950F`
- D1B:
  `build/final60/a6v4w_d1b_soa_trusted_pc_20260901/`, EBOOT
  `46969390B36A63BDEC5EBD7EF407020B27040A2A0845E8193CD2CFAA3F56B3C7`

Every directory carries its ELF, SFO, PRX, profile tuple and verified
`SHA256SUMS`.  Binary audits found the expected BS11/BS13 and trusted-reader
markers and no C1/C2/Effect/lean/eDRAM/PSP-1000 leakage.  BS13 reduces the two
seed banks' BSS by 14,336 bytes.  These are still PC-only artifacts and may not
be deployed before the hardware gates below pass. Earlier D1 artifacts from
2026-09-01 carry `DO_NOT_DEPLOY.md` and remain permanently superseded.

## Real-hardware queue

No D1 candidate is deployable from PC results alone.  After a separate
explicit `いれて`, run the profiles serially:

1. D1S0 gets a correctness run covering boot/self-test and the fixed replay
   window.  It must match RID30 gameplay/replay evidence and report zero
   protocol, fault or authority regression.  Its timing is not a D1 result.
2. Only after D1S0 passes, D1A gets the same correctness run for the BS13
   transpose, including exact stride/ABI and fallback evidence.  Its timing is
   also not a D1 result.
3. Only after both correctness gates pass, D1B gets the same-Lunatic Stage-6
   ACCEPT run.  Required evidence is zero protocol/fault/fallback regression,
   unchanged visuals and gameplay, identical replay window sequence, and a
   performance win over RID30.

A neutral or slower D1B is rejected and D: returns to RID30.  Results from
D1S0 or D1A must never be substituted for the required D1B performance result.

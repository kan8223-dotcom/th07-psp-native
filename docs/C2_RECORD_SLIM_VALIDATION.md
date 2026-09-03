# C2 compact-record slimming: independent validation contract

Status: PC gate passed on 2026-09-01; real-hardware gate remains queued.  This
document does not authorize a Memory Stick deployment or a PSP-1000 build.
The immutable comparison point is the accepted RID30/C1-OFF ELF.

## Scope and independent switches

C2 is three separately reversible ABI changes.  All switches default to zero
and must appear in the build profile stamp and in the SC and ME compiler
flags.

| Increment | Make switch | C/C++ switch | Selected ABI |
|---|---|---|---|
| C2a | `PSP_ME_BULLET_OUTPUT_SLIM` | `TH07_PSP_ME_BULLET_OUTPUT_SLIM` | output slot 4 B, output bank 4,224 B |
| C2b | `PSP_ME_BULLET_SEED_SLIM` | `TH07_PSP_ME_BULLET_SEED_SLIM` | Bullet seed slot 56 B, seed bank 57,664 B |
| C2c | `PSP_ME_ITEM_SEED_SLIM` | `TH07_PSP_ME_ITEM_SEED_SLIM` | Item seed slot 48 B, seed bank 53,632 B |

The public build matrix is `c2a_output_slim`, `c2b_bullet_seed_slim`,
`c2c_item_seed_slim`, and `c2abc_all_slim`.  The three single-feature builds
must differ from RID30 only in their named C2 switch plus build identity/title;
the cumulative build enables exactly all three C2 switches.  C1 UV16, C1
XYZ16, and the C1 GE experiment remain disabled in every C2 build.

## ABI oracle

All integer fields below use the PSP's 32-bit ABI.  Every arena remains a
multiple of one 64-byte cache line.

### C2a output

`Th07PspMeBulletCompactSlotResult` contains only:

- `generation`: unsigned 16-bit at byte 0
- `flags`: unsigned 16-bit at byte 2

The output bank is `candidateBits[32]` followed by 1,024 four-byte results:
128 + 4,096 = 4,224 bytes.  SC obtains the authoritative next position from
the already JIT-validated seed slot; removing three duplicated position words
must not weaken the full-u32 seed-generation check or allowed-flags check.

### C2b Bullet seed

The 56-byte slot offsets are generation 0; position 4/8/12; velocity
16/20/24; sprite size 28/32; graze size 36/40; next position 44/48/52.
`staticFlags` and `reserved` are absent.  After the 64-byte header are two
32-word planes (`candidateBits`, then `inBoundsBits`) and then 1,024 slots:
64 + 128 + 128 + 57,344 = 57,664 bytes.

For every selected candidate, the candidate plane supplies CANDIDATE and the
in-bounds plane supplies IN_BOUNDS.  A tail/reserved interpretation from the
old slot is forbidden.  Both planes have exactly 1,024 representable bits, so
all accesses are guarded by `slot < 1024`.

### C2c Item seed

The 48-byte slot offsets are generation 0; current position 4/8/12; start
position 16/20/24; target position 28/32/36; timer current 40; timer subframe
bits 44.  `stateAndFlags` and the three reserved words are absent.

After the 64-byte header are four 48-word planes in this order:
`candidateBits` (offset 64), `stateBit0` (256), `stateBit1` (448),
`autoCollectBits` (640), then slots at 832.  Total size is 64 + 768 + 52,800 =
53,632 bytes.  A candidate bit also proves `inUse == 1`; a separate in-use
plane is deliberately absent.  State is reconstructed as bit0 | bit1<<1.
Only states 0, 1, and 2 are legal.  State 3, autoCollect values greater than
one, and inUse values other than exactly one are capture-time soft rejects
rather than truncation: the producer omits that candidate and leaves the Item
on the canonical SC path.  If an injected/malformed published seed
encodes state 3, the worker returns the Item segment's RECORD result without
invalidating the independently valid Bullet segment.

Only 1,100 Item slots are valid.  The last valid slot is word 34 bit 11.
Words 35..47 and bits 12..31 of word 34 are padding.  Producers zero them;
consumers require them to remain zero, then never interpret them as slots.
No path may index word 48 or higher.

## PC gates

1. Compile the public header under OFF, each individual switch, and all-three.
   Assert exact sizes, alignments, member types, offsets, and plane offsets.
2. Compile all eight C2 switch combinations and prove the Bullet compact seed,
   Bullet output, and Item seed protocol-version tuple is unique.  A producer
   and consumer built with different layouts must therefore reject before
   reading a payload.
3. Audit producer and worker bounds, bitplane clear/set/get operations, Item
   padding validation, output generation/allowed flags, full-u32 seed
   generation validation, and C2a's use of seed `nextPos*Bits`.  In particular,
   a generation-bracket race must clear Bullet candidate, in-bounds bit, and
   slot payload together; leaving an orphan in-bounds bit rejects the entire
   seed instead of providing the intended slot-local SC fallback.
4. Audit every capacity, cache writeback/invalidate, hash, payload boundary,
   guard boundary, and pointer increment.  They must use the selected
   `sizeof(type)` or typed array indexing; no selected path may retain a
   64-byte slot stride or legacy arena byte count.
5. Audit fail-closed ordering: version/header/size/capacity/hash/bitplane
   rejection precedes slot access; a reject publishes no partially valid
   result; `committed` remains the last publication store.  Item rejection is
   segment-local and leaves Bullet processing/game startup available.
6. Build only the four PSP-3000 C2 profiles plus C2-OFF.  C2-OFF must be byte
   identical to the fixed RID30 ELF.  Inspect symbols and disassembly for the
   selected 4-, 56-, and 48-byte layouts and confirm C1 remains OFF.

## Real-hardware queue (not a PC gate)

No C2 candidate is deployable merely because the PC gate passes.  Each
increment remains individually selectable for one PSP-3000 M0/ACCEPT run.
Required runtime evidence is protocol/self-test PASS, zero RECORD/SEED/GUARD
faults, unchanged replay/simulation hashes, and unchanged Item/Bullet visuals
and behavior.  Performance comparisons use RID30 as the fixed baseline.  No
PSP-1000 build and no D:/H: operation belongs to this validation phase.

## Observed PC result (2026-09-01)

- Focused C2 tests: 17/17 PASS; related regression set: 77/77 PASS; full
  discovery: 633/633 PASS.
- The independent audit found one material defect before acceptance: a Bullet
  generation-bracket race cleared `candidateBits` and the slot but left the
  C2b `inBoundsBits` bit set.  The producer now clears all three atomically at
  the slot-contract level, and the extracted-kernel regression passes.
- C2-OFF ELF SHA-256 is
  `130db4a811d4d116ed9bba05960c245dd8731e88dca461c7fa65a38dccafd620`;
  it is byte-identical to the fixed RID30/C1-OFF ELF (`cmp` exit 0).
- PSP-3000 candidate ELF SHA-256 values are:
  C2a `554794c9e02007fcd218360afb9dc51d58eaf4af79fd89dd5f01ae61f44227db`,
  C2b `52c3dc6d21cd916d4f06371cd493f312c32b425167bc5ff2f2c3cd3ba31388e2`,
  C2c `5350d850537c2f82a6eeecedad7e38c33d7d92840ee0cdc6438d3b10d3f74e6c`,
  and cumulative C2abc
  `187440428db58b00422009ee0e2426536018b6f332697587d78d64a7106f5fb2`.
- ELF symbol sizes prove the selected layouts reached the PSP binaries.  With
  both 64-byte guards included, the output arena is 4,352 B slim / 16,640 B
  baseline; the two-bank Bullet seed array is 115,584 B slim / 131,712 B
  baseline; the two-bank Item seed array is 107,520 B slim / 141,568 B
  baseline.  Every single-feature and cumulative artifact matched exactly.

This is a PC-only GO.  Pixel/gameplay correctness, protocol/self-test health,
and timing improvement still require the separately authorized PSP-3000 run.

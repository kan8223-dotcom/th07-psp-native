# A6V4W stage 5 -> 6 font-tail archive fix

## Failure proved on hardware

The A6V4W Music Room font build reached the stage-5 clear transition, released
the stage text cache, and then failed twice while loading stage 6:

- `face_06_00.anm`: 3,072 KiB decompressed source
- total heap free: 5,392 KiB
- largest contiguous heap block: 10 KiB
- result: stage retry failed and the game returned to the title/XMB path

Evidence:
`artifacts/TH07PSP_BOOT.MUSICFONT-ST5-XMB.20260901-220850.LOG`
(SHA-256 `745EC82A1C89FBC77EE88D4BB3E951034116356F6D90B17F59F53A959A3E4DF4`).

This was fragmentation, not an out-of-memory total and not a regression in
the Music Room point-size fix. Earlier A6v4 testing returned from stage 5 to
the title and therefore never exercised a live stage-5 -> stage-6 transition.

## Ownership fix

The process-lifetime 5,411 KiB title/font arena already contains only the
300,364-byte local MS Gothic subset while gameplay is active. After stage
teardown, the stage text-cache tail is detached. The new opt-in
`PSP_FONT_TAIL_ARCHIVE` profile lends the 64-byte-aligned remaining arena tail
to one synchronous `face_*.anm` decode at a time.

- The resident font prefix remains untouched.
- `FileSystem` tries the tail before general-heap `malloc` for `face_*.anm`.
- `AnmManager` still requires immediate compaction/release; a source pointer
  cannot escape into gameplay.
- A nested loan, title transition, or overlap with stage text fails closed.
- Stale/duplicate interior-arena releases are consumed by the arena owner and
  can never reach `free()`.
- If a face archive does not fit, the established independent allocation path
  remains available.

The feature is default-off and profile-stamped, so accepted A6v4/A6v4W build
IDs retain their original behavior. It is enabled only by the new stage-6
candidate and future D2A SoA candidate.

## Verification

- Focused owner/font/title/Music Room suite: 75 tests passed.
- Full suite: 779 tests passed.
- ASan lifecycle cases cover the real 3 MiB stage-6 source, a 4.6 MiB upper
  case, nested refusal, abnormal stage-admission ordering, duplicate release,
  delayed stale release after font demotion, and font-prefix preservation.
- PSP-3000 build passed with build ID `0x260901ad`.
- MECC proven-payload audit passed.
- Rebuilding the accepted Music Room profile with the new feature disabled
  reproduced its archived EBOOT byte-for-byte
  (`B5BB8A199063DA414AB6286A38B0BE32E00C4648044E2EEBB26394E8E8380A1E`).
- PSP-1000 was not built.

## Hardware acceptance gate

Use the exact reproducer: a live (non-replay) Sakuya-A route (`ply02a.sht` in
the failing log) through the stage-5 boss clear into stage 6. The Reimu
Lunatic replay is not an acceptance substitute because it does not reproduce
the fragmented heap layout. On the old build, its `ply00a` route loaded the
same 3,072 KiB face successfully; the Sakuya failure run carried about 944 KiB
more heap allocation at the stage-5 teardown boundary. Reimu evidence:
`artifacts/TH07PSP_BOOT.A7-REIMU-LUNATIC-NOREPRO.20260901-223639.LOG`
(SHA-256 `5910ECA8C0C8D429FB18E352D8CF0EC15E92FC5144F7C169AEBD7BA3356076BE`).
The acceptance log must contain an
`A6V4 ARENA lease=ARCHIVE-TAIL` / `release=ARCHIVE-TAIL` pair for
`face_06_00.anm`, no `ARC ALLOC NG`, and normal stage-6 entry. Music Room,
wave-dash rendering, ME/Item paths, audio, and the usage meter must remain
unchanged.

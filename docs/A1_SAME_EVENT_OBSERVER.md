# A1-SAME event observer

Status (2026-09-02): PC validation and hardware collection complete.  The run
had clean telemetry/closure but incomplete event coverage and a severe Stage 6
FPS regression, so the candidate was rejected and D: was rolled back to the
exact previous D2B.  This observer build is not approved for re-deployment.

## Purpose and authority boundary

The A1-SAME observer attributes the cost and visible side-effect counts of the
existing authoritative SC bulk-event paths.  It does not move work to ME,
change allocation, reorder item or popup creation, or change any gameplay
decision.  Each timed bulk function reads the clock once at entry and once at
exit, then publishes one aggregate sample.  The bomb path likewise has no
per-bullet clock read.

`PSP_PERF_A1_SAME` is default-off and is accepted only for PSP-2000+,
`PSP_PERF_DIAG=1`, and `PERF_ACCEPT`.  The fixed D2B target pins it to zero;
only the dedicated target enables it:

```text
make psp3000-a6v4w-d2b-a1-same-observer-build
```

The candidate is D2B plus the observer, build ID `0x260902a1`, with C1/C2 and
other unrelated experiments still disabled.

## Sparse record

An A1 record is emitted immediately after its matching `PERF ACCEPT` record
only when the window contains A1 activity:

```text
PERF PFABME RIDxxxxxxxx Wn A1S Kxx LABELc/us/e/a/i/p/x/reasons/modes ... G1 O0
```

`RIDxxxxxxxx` is the runtime process nonce (for the captured run it was
`0065CBF0`), not the compiled build ID `0x260902a1`.

`Kxx` is the active-kind bit mask.  Inactive tuples are omitted.

| Bit | Label | Timed path |
|---:|---|---|
| 0 | `RAB` | `RemoveAllBullets` |
| 1 | `DSP` | `DespawnBullets` |
| 2 | `RAD` | `RemoveBulletsInRadius` |
| 3 | `RAE` | `RemoveAllEnemies` |
| 4 | `BUP` | bomb-active Bullet update traversal |

Tuple fields are, in order: calls, inclusive microseconds, eligible subjects,
affected subjects, item-spawn attempts, popup attempts, auxiliary count,
reason mask, and mode mask.  `eligible` means subjects that passed the
existing active/state filters; it is not a common fixed-pool visit count.
`auxiliary` is kind-specific: cleared/despawned lasers for `RAB`/`DSP`,
projectile enemies for `RAE`, and the summed bomb-clear high-water value for
`BUP`.

`G1` requires valid single-use reason ownership and no counter overflow.
`O0` requires no saturating-counter overflow.  The widest possible tagged
record remains below the 640-byte RAM-log line limit.  Whole-run capacity is
still proved only by the terminal `END VALID=1 DROP=0` record.

## Reason masks

| Bit | Name | Source event |
|---:|---|---|
| 0 | `UNKNOWN` | missing/invalid/double reason marker; invalidates `G` |
| 1 | `BEGIN_SPELL` | spell start |
| 2 | `SPELL_END` | spell completion cleanup |
| 3 | `BOSS_DEFEAT` | boss/midboss defeat cleanup |
| 4 | `SPELL_TIMEOUT` | spell timeout cleanup |
| 5 | `ECL_ENEMY_CLEAR` | ECL all-enemy clear |
| 6 | `ECL_BULLET_ITEM` | ECL bullet-to-item clear |
| 7 | `ECL_RADIUS` | ECL radius clear |
| 8 | `ECL_BULLET_FADE` | ECL bullet fade clear |
| 9 | `DIALOGUE` | dialogue cleanup |
| 10 | `RESPAWN_GRACE` | respawn grace clear |
| 11 | `FULL_POWER` | full-power clear |
| 12 | `BOMB` | bomb-clear traversal |

Reason and mode masks are OR-reduced within one 120-frame window.  If a kind
has more than one reason or mode bit in that window, its kind totals remain
valid but reason-specific attribution is incomplete.  The analyzer reports
such windows as `mixed-window`; it never invents a reason-to-time split.

## Hardware collection gate

Use one natural ACCEPT run that covers at least spell end, boss defeat, bomb,
radius clear, and all-enemy clear.  Also exercise the existing D2B lifecycle
checks (pause/resume, restart, and stage 5 to 6) where practical.  After
pulling the active root log through `tools/psp_stick.py`, run:

```text
python3 tools/analyze_a1_same_perf.py LOG \
  --require-reason SPELL_END \
  --require-reason BOSS_DEFEAT \
  --require-reason BOMB \
  --require-reason ECL_RADIUS \
  --require-reason ECL_ENEMY_CLEAR
```

Acceptance requires all of the following:

1. terminal `END VALID=1 DROP=0`;
2. every paired ACCEPT record has `V1`;
3. every A1S record has `G1 O0` and immediately follows its ACCEPT record;
4. D2B closure remains clean (`PSRA = PSRH + PSRF`, `PSRX` fault/disabled
   fields are zero, `PSME` SoA jobs are nonzero) and all
   ME/protocol/audio/allocation fault counters remain zero;
5. item count/order, popup behavior, score/PIV/SFX, visuals, and transitions
   remain unchanged.

The analyzer intentionally returns `performance_claim: false`.  Event-side
counter work is inside the measured regions, so this run may identify the
large event paths and their upper bounds, but A1S formatting and RAM-log append
occur after the matching window's `ELUS` is sealed.  They can still contribute
to `MISS` when they cross a VBlank.  A claimed speedup requires a separate
observer-off A/B build.

The analyzer accepts D2B's fixed `PSV...PSME` ACCEPT extension locally and
checks its classification/read closure.  The frozen RID30 A/B parser remains
strict and unchanged.

## Hardware result and disposition

- log: `artifacts/TH07PSP_BOOT.A1-SAME-FPS-REGRESSION.20260902-012748.LOG`
- SHA-256: `9F8EF68627F4E544ECD4FCB8E1E2A2D9B8CA1D1F91B0CF06DFFFCB79247208AB`
- 269 windows / 32,235 frames / `END VALID=1 DROP=0`
- actual FPS `48.518943`, MISS `7,596`, minimum-window HWFPS `29.9`
- 55 A1S windows, all `G1 O0`; `BOSS_DEFEAT` and `ECL_RADIUS` were not covered
- D2B reads: `PSRA=8,980,574`, `PSRH=2,413` (0.026869%),
  `PSRF=8,978,161`; fault/disabled zero
- D: rollback readback: D2B `1612A5F4...25DEC9`
- rejected A1 backup: `artifacts/stick_backups/EBOOT.TH07SHIKI.PRE-20260902-013029.FBDA4ECD.PBP`

The reported actual FPS is therefore the established `sum(N) / sum(ELUS)`
window metric, not whole-replay wall time or every observer cost.  Log
timestamps estimate the out-of-window time at 0.137616 s for this run versus
0.099961 s for old RID30 (about +0.037655 s, with 1 ms timestamp rounding), far
too small to explain the +91.053675 s difference in summed `ELUS`.

The observer-off D2B same-replay gate subsequently completed at 53.088591 FPS,
MISS 4,168, and OVR 4,015.  All `W/S/ST/N` fields and all D2B extension fields
matched the A1 run window by window, while removing A1 reduced summed `ELUS` by
57.187075 s, MISS by 3,428, and OVR by 3,559.  This isolates and confirms the
A1 observer build's performance regression.  D2B still trails the old RID30
endpoint by 33.866600 s and 2,032 MISS; a D2B-only verdict therefore still
requires a same-replay SoA-free A7 control.  Full follow-up evidence is in
`build/final60/a6v4w_d2b_a1_same_observer_pc_20260902/D2B_OBSERVER_OFF_SAME_REPLAY_20260902.md`.

PC aggregation also resolved why the embedded D2B reader hit only 0.026869%:
`PSWD/PSM=2,784,467/9,025,420` (30.851384%) proves that the eligible set was
not empty.  The dominant fused-capture read runs after `BeginCalc` but before
`EndCalc`, while the manager-wide read gate is deliberately closed, so it must
fall back before testing the slot's deferred bit.  Full analysis is archived as
`build/final60/a6v4w_d2b_a1_same_observer_pc_20260902/HARDWARE_RUN_20260902.md`
and `SOA_READ_GATE_ANALYSIS_20260902.md` in the same directory.

## PC, package, and deployment evidence

- candidate build gateのfull test discovery: 817/817 passed;
- hardware後のanalyzer/MISS-map hardeningを含むfull discovery: 840/840 passed
  （analyzer + MISS-map targeted 28/28）;
- feature-off D2B rebuild is byte-identical to the known D2B EBOOT, ELF, and
  DATA.PSP;
- two clean observer builds reproduced byte-identical EBOOT, ELF, and
  DATA.PSP;
- raw EBOOT SHA-256:
  `8dd3b7ce14504fd33ec3e18bceba725545ef8f0e1f2b20c3cd8cb0d2f14601dd`;
- DATA.PSP SHA-256:
  `0c31cc5dc9d67870b9448af05c2b0f61eb3ab85c4bd2e598e820560da3c34657`;
- ELF SHA-256:
  `78c3c7e8f2a62d8bd36d218bc6789224657d4260ffce4e8e0f2523defa690d11`;
- XMB PBP SHA-256 and D: readback:
  `fbda4ecd8ac32a272a2987578bc2d736158db5d98bafd9ff02fc278745277659`;
- XMB slots 0-5 and 7 came from accepted RID30; only slot 6 DATA.PSP was
  replaced.  SFO `MEMSIZE=1` and all eight slot identities were checked;
- MECC known-good render-worker payload audit passed;
- previous D2B backup:
  `artifacts/stick_backups/EBOOT.TH07SHIKI.PRE-20260902-004312.1612A5F4.PBP`;
- artifact archive:
  `build/final60/a6v4w_d2b_a1_same_observer_pc_20260902/`.

H: and `TH07SHIKI_NOME` were not modified.

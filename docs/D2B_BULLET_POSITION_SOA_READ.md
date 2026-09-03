# D2B Bullet position SoA read correctness gate

Status: correctness path ran cleanly, but the observer-off D2B endpoint is
hardware performance-rejected.  The D2B-based A1-SAME observer was separately
rejected, and its same-replay overhead is isolated.  The SoA-free A7 control is
now current on D:/TH07SHIKI (readback `9B58C04F...1D83C5`) and its same-replay
run is valid.  It isolates a material combined D2A+D2B SoA-lineage penalty, not
the D2B reader alone, and remains behind old RID30.  PC counter/call-site
analysis proved that the dominant SC reader is wired inside the interval in
which the manager-wide SoA read gate is deliberately closed.  D2C is sealed by
the 2026-09-02 phase decision.

## What changes

D2B keeps every canonical `Bullet::pos` AoS write from D2A. Eligible
post-calc SC readers now request XYZ through one slot-aware accessor. The
accessor validates slot ownership, active/generation identity, manager and
completed-calc serials, and the D2A deferred-eligible bit before accepting the
SoA value.

Every accepted SC read compares the raw IEEE-754 XYZ words with AoS first.
This includes signed zero and NaN payloads. One mismatch disables SoA reads
for the rest of the process, invalidates readable sidecar state, and continues
gameplay through canonical AoS.

The Bullet calc traversal itself always reads/writes AoS. SoA becomes readable
only after the complete 1,024-slot traversal. Pause, retry, demo restart,
manager reset and slot reuse revoke read authority before ownership can be
reused.

## ME direct-list ABI

D2B versions the direct-list position source instead of leaving ME on an
unversioned `Bullet::pos` offset:

- D2B: `LL02` list layout, 204 bytes; `PS01` position source, 72 bytes.
- Feature off: frozen `LL01`, 128 bytes, remains byte-for-byte compatible.
- `PS01` carries AoS or SoA owner bounds, XYZ planes/stride, validity and full
  generation sources, and manager/calc serial brackets.
- A clear SoA valid bit permits per-slot AoS fallback. A set bit is a promise;
  stale/torn identity rejects the whole job with zero output.
- The descriptor owner must not alias input, output or run pools.

The existing conservative full D-cache fence before a direct-list command
publishes the SoA planes to ME. ME output ownership and GE submission are
unchanged.

## Telemetry and fail-closed verdict

The existing D2A telemetry remains. D2B adds:

- `PSRA`: SC position read attempts;
- `PSRH`: accepted raw-bit-equal SoA reads;
- `PSRF`: canonical AoS fallbacks;
- `PSRX=fault/disabled/readableSerial`;
- `PSME=SoA-jobs/AoS-jobs` for the ME direct-list position source.

Hardware closure requires `PSRA = PSRH + PSRF`, zero read faults, reads not
disabled, and at least one ME SoA job in ordinary gameplay. Existing ME
correctness, mismatch, protocol, audio and allocation faults must remain zero.

## Build and PC evidence

Build target:

```text
make psp3000-a6v4w-d2b-position-soa-read-build
```

Profile identity:

- PSP-3000 profile / PSP-1000 explicitly disabled;
- build ID `0x260901ae`;
- SFO title `TH07 A6V4W D2B POS SOA READ`;
- A6v4W local-font and A7 face-archive tail fixes retained;
- C1, C2, D1 seed SoA, Effect ME, eDRAM and lean-cache experiments disabled.

Evidence:

- full test discovery: 797/797 passed;
- two clean builds reproduced byte-identical EBOOT and ELF;
- EBOOT SHA-256:
  `1612a5f47abe0451966cf37519841cbc69756f1a482325473aa611b18225dec9`;
- DATA.PSP SHA-256:
  `0a6e656e85877ab0dc578d5a06ab3a116dc74be2e708741f7c6aa3550cbca64c`;
- ELF SHA-256:
  `f52917308609de9daebff8a7b6ba55884c1740965dd9c71a06bd6ddfdef34ae3`;
- MECC known-good render-worker payload audit passed;
- artifact archive:
  `build/final60/a6v4w_d2b_position_soa_read_pc_20260901/`.

## Hardware evidence and disposition

The recovered hardware log
`artifacts/TH07PSP_BOOT.PULL.20260901-234728.LOG` reaches the title and covers
stage 4 through part of stage 5.  It shows `MERW STREAM DIRECT PS01 PASS`,
`PSME>0`, `PSRA>0`, zero `PSRX` fault/disabled fields, `MEFAULT=0`, and terminal
`END VALID=1 DROP=0`.  This proves that the SoA path ran on the PSP, but it is
not a performance verdict or the complete lifecycle gate.

The later A1-SAME Stage 6 log contains the same D2B reader and is important
warning evidence, although it is not a D2B-only comparison.  Across 269 windows
it recorded `PSRA=8,980,574`, `PSRH=2,413`, and `PSRF=8,978,161`: only
0.026869% of reads hit SoA and 99.973131% paid the reader path before falling
back to AoS.  Window-level CPU regression versus the old RID30 run correlates
0.8983 with reads/frame.  The deployed combination measured 48.518943 FPS and
7,596 VSync misses.  Because that endpoint also contains A1 and inherited
post-RID30 changes, it does not by itself prove a D2B-only regression.

The formal PC aggregation rejects the alternative explanation that the
eligible set was nearly empty.  Across the run, `PSM=9,025,420` exact matches
contained `PSWD=2,784,467` deferred-eligible matches (30.851384%), while only
1,340 entry-eligible records became unsupported at final publication.
Nevertheless, `PSRH/PSWD` was only `2,413/2,784,467` (0.086659%).  In W12-W15,
`PSWD=248,136`, `PSWU=0`, and all 318,449 reads fell back.

The reason is structural: `PspBulletPositionSoaBeginCalc` clears the readable
serial and sets `traversalActive`; the per-slot fused capture then calls the
SC accessor immediately after publishing that slot but before the full
traversal reaches `PspBulletPositionSoaEndCalc`.  The accessor therefore
rejects before consulting the slot's deferred bit.  Ordinary update
publications excluding spawn (`PSP-PSS=8,974,528`) account for 99.932677% of
all `PSRA`, and 232/269 windows have `PSRA == PSP-PSS`.  The low-hit
classification is consequently **SC reader gate wiring/timing failure**, not
an empty eligible set.  Full evidence is archived in
`build/final60/a6v4w_d2b_a1_same_observer_pc_20260902/SOA_READ_GATE_ANALYSIS_20260902.md`.

The observer-off same-replay run is archived as
`artifacts/TH07PSP_BOOT.D2B-OBSERVER-OFF-SAME-ST6-RUN1-20260902.20260902-024714.LOG`
(SHA `CA24272D...DBFA74`).  It sealed at 269 windows / 32,235 frames with
`END VALID=1 DROP=0`, 53.088591 FPS, MISS 4,168, and OVR 4,015.  Every D2B
counter matched the A1 run per window, but removing A1 recovered 57.187075 s
and 3,428 MISS.  The A1 performance rejection is therefore isolated.

Against old RID30, standalone D2B still has +33.866600 s, +2,032 MISS, and
+2,140 OVR.  Excluding accepted dense W12-15 still leaves +31.770558 s and
+1,906 MISS.  This rejects the D2B endpoint operationally.  It does not assign
all residual time to the accessor because D2A validation/publication and other
post-RID30 inherited changes covary with bullet load.  Detailed evidence is in
`build/final60/a6v4w_d2b_a1_same_observer_pc_20260902/D2B_OBSERVER_OFF_SAME_REPLAY_20260902.md`.

The standalone D2B EBOOT was current on D:/TH07SHIKI after the A1 rollback and
was preserved at
`artifacts/stick_backups/EBOOT.TH07SHIKI.PRE-20260902-004312.1612A5F4.PBP`.
Immediately before the later A7 deployment, the same D2B readback was preserved
again at
`artifacts/stick_backups/EBOOT.TH07SHIKI.PRE-20260902-030754.1612A5F4.PBP`.
Its feature-off rebuild during A1-SAME work again matched the known D2B EBOOT,
DATA.PSP, and ELF byte for byte.  D:/TH07SHIKI is now A7
`9B58C04F5BECF1BA2438D7C74E65BD4B0B1D1AA7206308702F785D99741D83C5`;
H: and TH07SHIKI_NOME were not modified.

The SoA-free A7 same-replay control is archived as
`artifacts/TH07PSP_BOOT.A7-SAME-ST6-RUN1-20260902.20260902-032235.LOG`
(SHA `C3CB00BF7DCB1EEF1800C6843C55C62DCB841FB8684E810B2BC2145079FC7E38`).
It sealed at 269 windows / 32,235 frames with all
`W/S/ST/N` identical to D2B and old RID30, all ACCEPT `V1`, `MEFAULT=0`, and
`W269 END VALID=1 DROP=0`.  A7 measured `sum(ELUS)=583.704968 s`, 55.224817
FPS, MISS 2,758, OVR 2,526, and minimum HWFPS 33.4 at W15.

Relative to observer-off D2B, A7 recovered 23.487642 s, 1,410 MISS, and 1,489
OVR while gaining 2.136226 FPS.  This same-replay one-difference comparison
isolates the combined D2A+D2B SoA lineage because A7 removes both D2A
shadow/validation/publication and the D2B reader/accessor; it does not isolate
the reader alone.  Relative to old RID30, A7 still has +10.378958 s, +622 MISS,
and +651 OVR (FPS -0.999738).  Even excluding accepted dense W12-15, the
residual is +9.831187 s / +589 MISS / +628 OVR.  A7 is therefore a strong
partial recovery, not RID30-equivalent performance closure.

## D2C disposition: sealed

ME currently sees the sidecar's broad `validBits`, while the SC reader also
requires the narrower D2A `deferredEligibleBits`. This is safe in D2B because
AoS is still fully maintained and the correctness gates compare both paths.
Before any hypothetical D2C build could remove AoS XYZ writes, ME authority
would have to use the same narrow eligible bitmap (or an equivalent separately
versioned owner), and the D2B hardware gate above would have to pass first.
The MISS-hunting phase explicitly forbids that new implementation, so these
conditions are retained only as historical safety rationale.

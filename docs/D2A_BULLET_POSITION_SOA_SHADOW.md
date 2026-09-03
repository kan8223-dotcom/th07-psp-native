# D2A Bullet persistent-position SoA shadow

Status: PC correctness/coverage increment only. It does not authorize a
Memory Stick deployment, a performance claim, or a PSP-1000 build.

## Purpose

D1 changed only the transient ME seed layout. D2A begins the persistent
Bullet-position conversion: it keeps an SC-owned XYZ SoA sidecar across calc
passes and proves that the sidecar has the same raw bits as canonical
`Bullet::pos` at every active-slot visit.

AoS is still authoritative in D2A. Every original AoS read and write remains;
the sidecar is never consumed by gameplay, collision, rendering, ME or GE.
Therefore D2A can measure coverage and lifetime correctness but cannot improve
FPS. A neutral or slower D2A result is expected and is not a SoA performance
result.

## Layout and identity contract

`src/PspBulletPositionSoa.hpp` defines a 64-byte-aligned 25,152-byte sidecar:

- 1,024 logical slots and a 1,024-bit validity bitmap;
- six raw-u32 planes: full slot generation, manager serial, calc serial, X,
  Y and Z;
- 1,040 words per plane. The extra 64-byte line rotates equal slot indices
  across D-cache sets instead of repeating every 4 KiB;
- raw IEEE-754 bit comparison, including distinct `+0.0` and `-0.0`;
- validity published last, and any identity or bit mismatch fails closed.

The runtime adds a separate 128-byte bitmap recording which valid slots ended
the preceding pass in the proven deferred-eligible subset. This distinguishes
mere AoS observation from an operation that would really have to revoke SoA
authority after D2C.

The sidecar does not reuse D1's ME seed. D1 is per-frame transport state;
D2A is persistent SC ownership state. The D2A build deliberately keeps D1
seed SoA/trusted authority, C1, C2, Effect ME and eDRAM experiments off.

## Lifecycle hooks

- `BulletManager::Initialize`: advance manager identity and reset all planes.
- slot track/forget: advance the existing full generation, then invalidate
  that slot.
- spawn: publish only after final position/backshift, `RunCommands`, immediate
  despawn handling and screen-clear state are settled. Spawn never marks a
  slot deferred; it must first survive one exact eligible update pass.
- ordinary update: validate at the active-slot entrance and publish final XYZ
  at `update_timers`, after all canonical movement/commands.
- normal pause/retry: `GameManager` clears the shadow before breaking the calc
  chain; time-stop also clears through `BulletManager`.
- demo low-priority restart: both restart branches clear before returning
  `RESTART_FROM_FIRST_JOB`, because Bullet update can run twice in one draw.
- bulk clear/item conversion, bulk despawn and radius query: validate before
  the first AoS position read or state overwrite, record the future
  materialization reason, then conservatively return the slot to AoS ownership.

Teardown needs no materialization while D2A remains observational. A later
authoritative phase must drain ME/GE readers before discarding ownership.

## Telemetry and acceptance

The PSP hardware `PERF ACCEPT` line keeps its existing real system-clock
`HWFPS/ELUS`; replay `curFps`, legacy SHIKIGAMI `FPS=` and PC replay speed are
not accepted as performance inputs. D2A adds:

- `PSV/PSM/PSC`: active visits / exact matches / cold-invalid visits;
- `PSWD/PSWN/PSWU`: currently eligible matches / unsupported matches /
  materializations required because a preceding deferred slot or a slot that
  was eligible at entry became unsupported by final publication;
- `PSP/PSS/PSI`: all publishes / spawn publishes / invalidations;
- `PSMV/PSMM/PSMC/PSMD/PSMK/PSMF`: external mutation visits / exact matches /
  cold / previously deferred / already canonical / faults;
- `PSMR=a/b/c/d`: actual deferred-authority revocation candidates for bulk
  item clear, despawn transition, bulk despawn and radius query;
- `PSX`: manager/generation/calc/position/slot/publish fault tuple;
- `PSB`: pause/restart clear events; `PSBM`: deferred slots revoked by those
  pause/restart boundaries; `PSR`: manager resets/calc passes;
- `PSVC`: current valid slots; `PSG`: D2A closure verdict.

`PSG=1` requires exact visit classification, exact mutation-reason closure,
valid slot bounds and zero identity/position/publish/mutation faults. Faults
remain sticky across ordinary 120-frame log windows.

## Promotion gates

Build target:

```text
make psp3000-a6v4w-d2a-position-soa-shadow-build
```

D2B hardware deployment/promotion is blocked until one explicit correctness
run covers dense stage 6 play, pause/resume, demo restart, spawn/reuse and mass
cancel with `PSG=1` and zero `PSX/PSMF`. PC-only implementation may proceed
behind a separate default-off gate, but may not remove AoS writes or make a
performance claim. Coverage must also show that `PSWD` is large enough to
justify the conversion after accounting for `PSWU` and `PSMR`.

D2B will change readers to slot-aware accessors while retaining AoS writes.
D2C may omit AoS XYZ writes only for the proven safe NORMAL subset. Before
either phase can become authoritative, the ME direct-list ABI must be
versioned: it currently reads `Bullet::pos` and adjacent AoS state directly.

# C1: ME render-stream 16-bit vertex validation

Status: PC design/audit contract.  No Memory Stick deployment is authorized by
this document.  The fixed comparison source is RID30 A1-MOVE:

- XMB PBP: `743c14cc13c1188b2c56cc0d3f98e0d559b3d7e2eb645c28556fe1a294edacbc`
- `DATA.PSP`: `69c76b5137e5a206b666035450c1c4fdb1b0643096063f97952360b250ed31af`
- ELF: `130db4a811d4d116ed9bba05960c245dd8731e88dca461c7fa65a38dccafd620`

The experiment applies only to the command-10 ME render stream.  It does not
change AnmManager's canonical 24-byte Main-RAM arena, non-ME draws, game-state
math, primitive order, color, texture contents, or SC authority.

## 1. Hardware facts and byte budget

The local PSPSDK prose groups 16-bit texture, normal and position components
together as signed values, but that prose is not accurate for texture
coordinates.  PPSSPP's GE vertex decoder provides the independently auditable
hardware contract used by this experiment:

- `GU_TEXTURE_16BIT` is read through `u16_le` and decoded as `q / 32768` in
  PPSSPP commit `1ee916871104a094093abd35d05122d18cea5923`,
  `GPU/Common/VertexDecoderCommon.cpp::Step_TcU16ToFloat` (and the same
  unsigned interpretation is used by `Step_TcU16Prescale`).
- `GU_VERTEX_16BIT` is read through `s16_le` and decoded as `q / 32768` in
  `Step_PosS16`.

Real PSP readback remains the final authority, but the PC implementation must
therefore represent UV as unsigned and XYZ as signed.  Components are ordered
texture, color, position and must be at least 16-bit aligned.
`GU_TRANSFORM_3D` still applies the model/view/projection matrices;
`GU_TRANSFORM_2D` bypasses the transform and is not an equivalent replacement
for TH07's depth-aware screen-space path.

With `GU_COLOR_8888` preserved, the four compile-time layouts are:

| M0 step | GE declaration | C layout | bytes/vertex | saving |
|---|---|---|---:|---:|
| OFF | UV float, RGBA8888, XYZ float | `f32 u,v; u32 color; f32 x,y,z` | 24 | 0% |
| UV16 | UV u16, RGBA8888, XYZ float | `u16 u,v; u32 color; f32 x,y,z` | 20 | 16.7% |
| XYZ16 | UV float, RGBA8888, XYZ s16 | `f32 u,v; u32 color; s16 x,y,z; pad16` | 20 | 16.7% |
| BOTH | UV u16, RGBA8888, XYZ s16 | `u16 u,v; u32 color; s16 x,y,z; pad16` | 16 | 33.3% |

The maximum is therefore **24 to 16 bytes (33.3%)**, not 50%.  Every layout
must have compile-time size/alignment/offset assertions on both the SC and ME
translation units.  Output capacities, byte counts, hashes, cache ranges,
run validation and GE pointer increments must use the selected vertex size;
no literal `24` may survive in a selected-layout calculation.

## 2. Independent and reversible switches

Use two independent default-off switches, one for UV and one for XYZ.  BOTH is
the composition of the two switches, not a third implementation.  Each switch
must be part of `PROFILE_STAMP`, forbidden for PSP-1000, and restricted to the
same PSP-3000 PERF-ACCEPT/ME render-worker prerequisites as RID30.  OFF must
compile to the exact RID30 path.

Changing the stream ABI requires a distinct version/build identity.  A stale
24-byte producer must never be accepted by a 16/20-byte consumer.  A rejected
conversion or unsupported finite range fails closed before READY publication:
the complete ME stream is discarded and the canonical SC draw is used.  Do
not publish a partially converted prefix in C1.

## 3. Candidate numeric mapping

The following is the starting candidate, not a claim of pixel equivalence.
The real GE M0 gate in section 5 decides whether nearest, truncation or another
conversion matches the float path.

### UV16

- Candidate payload: `q = round_to_selected_mode(uv * 32768)`.
- Store `q` as `u16`, not `s16`.  Accept only finite, non-negative inputs for
  which the rounded result is in `[0, 65535]`; a conservative implementation
  bound is `scaled >= 0 && scaled < 65535.5`.
- Keep the existing per-texture `sceGuTexScale(sampleScaleX, sampleScaleY)` and
  zero texture offset.  No clamp or saturation is allowed: an out-of-range
  value rejects the candidate stream and takes the float SC fallback.
- `uvStart + uvScrollPos` can exceed one even though `uvScrollPos` is wrapped;
  UV=1 is legal (`q=32768`) and the representable interval extends to just
  under 2.  Negative UV and UV=2 must reject.  Range validation is therefore
  a real runtime contract, not debug-only.

### XYZ16

Keep `GU_TRANSFORM_3D` and the existing orthographic view/projection.  A
non-uniform model scale is required because x/y are logical pixels while z is
a small depth value:

- x/y candidate: `q = round_to_selected_mode(value * 32)`, model scale 1024.
- z candidate: `q = round_to_selected_mode(value * 32768)`, model scale 1.
- Candidate model matrix: `diag(1024, 1024, 1, 1)`, not a uniform 1024 scale.
  A uniform scale would turn the normal Item/Bullet depths 0.01/0.05 into a
  coarse 1/32 grid and can change occlusion.
- Reject non-finite values and values outside the exact signed range.  Do not
  clamp or wrap.

The candidate matrix must be installed for every GE list segment that consumes
XYZ16 vertices, including after an internal list-space restart.  It must be
restored to identity at the end of the ME submission and on every pre-submit
failure path.  Renderer matrix-cache state must agree with the physical GE
state after both operations.  A later normal sprite draw must never inherit
the C1 scale.

### C4実機結果とC5のFCR修正（2026-09-01）

C4 (`0x260901c4`, M0F) は実機で N=0 を通過し、N=128 の command-10
M0で停止した。診断mask `0x00048002` は RESULT / OUTPUT_BYTES /
FCR_RESTORE で、一次故障はFCR31復元不一致だけだった。旧UV packerの
float比較と `scaled + 0.5f` → `trunc.w.s` がMEのFCR31を変更し、workerが
正常結果をPROTOCOLへ降格したためである。M0は `GE0 READBACK-PENDING` の
段階なので、GE consumerや実描画はこの停止に関与していない。

C5 (`0x260901c5`, M0I) はproduction UV packをIEEE754のraw bitsから
Q15へ変換する整数演算へ置換した。負の非zero、NaN/Inf、round後65535超は
従来どおりrejectし、±0は0として許可する。旧binary32演算との完全byte互換
のため、唯一の二重丸め例外 `bits == 0x377fffff` は明示的に1へ変換する。
この例外はE=-17の全仮数、E=-16..0の全accepted値をfloat32 RN-evenと照合して
一意と確認済みである。M0の独立oracleはfixture生成時の既知uQ/vQを直接書き、
production packerもCOP1も使用しない。C5のPSP逆アセンブルでは
`me_render_stream_write_vertex` 内にCOP1演算・比較・変換が存在しないことを
確認した。C5も実機M0/GE readback/performanceを通るまではPC候補に留まる。

## 4. PC gates

PC tests can prove packing and contracts, but cannot prove PSP GE raster
identity.  They must cover all of the following:

1. OFF/UV16/XYZ16/BOTH Make profiles, independent defines, profile-stamp
   separation, PSP-1000 rejection and fixed RID30 comparison metadata.
2. Exact struct sizes, alignment and offsets on host, SC Allegrex and ME
   Allegrex objects.
3. Independent reference conversion over zero, UV=1, unsigned UV extrema,
   signed XYZ extrema, values immediately around half steps, atlas half-texel
   UVs, x/y half pixels and normal z values 0.01/0.05.  Negative UV, NaN,
   infinity and overflow must reject without READY publication.
4. Color bytes, run order, primitive, source, blend and z-write flags remain
   unchanged.  OFF continues to byte-compare the native 24-byte stream.
5. For each selected layout, completion byte count, output hash, guard region,
   cache writeback/invalidate range and READY-view validation cover exactly
   `vertexCount * selectedVertexBytes`.
6. Worker self-test and a separate PC oracle compare every packed word.  The
   oracle must not call the production pack helper.
7. Internal GE-list restart and normal-draw-after-C1 tests prove scale
   reinstallation and identity restoration structurally.
8. Full Python suite, PSP-3000 build and binary audit.  Do not build PSP-1000.

No PC pass may be reported as pixel or hardware pass.

## 5. Real-hardware M0 staircase

Run one independently identified candidate at a time in this order:

1. OFF (RID30 behavior and instrumentation overhead baseline)
2. UV16 only
3. XYZ16 only
4. BOTH, but only if both preceding steps pass

Each step needs its own RID/SHA and the same replay/input source.  Never infer
BOTH from the two individual passes.  A failed step is rejected independently;
for example UV16 may survive even if XYZ16 fails.

Before a gameplay performance claim, a synthetic M0 harness must render float
and candidate versions to isolated equal surfaces in one boot and compare
readback bytes.  The corpus must include:

- GU_SPRITES axis pairs and indexed GU_TRIANGLES quads;
- rotated and mirrored quads, cull-edge coordinates, negative coordinates;
- x/y offsets around every 1/32 and raster half-pixel boundary;
- atlas half-texel edges, UV scrolling, values around zero/one/two, negative
  UV rejection and linear filtering;
- alpha and additive blending;
- Item z=0.01, Bullet z=0.05, overlapping depth-order sentinels and z-write
  enabled/disabled cases;
- a forced display-list restart inside an XYZ16 submission followed by a
  normal 24-byte sprite.

Acceptance is exact color framebuffer equality and an exact depth result.  If
direct depth-buffer readback is unavailable, use an occlusion sentinel whose
final color uniquely identifies the depth winner.  Tolerance, SSIM and
"visually unchanged" are not substitutes for this gate.

Then compare framebuffer hashes on real mixed-geometry replay frames: Stage 6
W12-W15 dense bullets and the Yuyuko Item-conversion W253/W254 event.  At least
one chosen frame must contain both GU_SPRITES and indexed rotated quads.  A
format can advance to performance measurement only with zero color/depth
mismatch, zero guard/hash/protocol fault and deterministic repeat hashes.

## 6. Performance and binary audit

M0 logs must separately report selected format, accepted/rejected vertices,
range rejects, output bytes, ME kernel/writeback cycles, SC invalidate/submit
time and matrix submissions.  The useful quantity is total SC frame time and
ME deadline margin, not byte saving alone.  A new matrix-state cost or higher
fallback rate can erase the theoretical writeback saving.

Binary audit must confirm:

- selected `GU_TEXTURE_* | GU_COLOR_8888 | GU_VERTEX_* | GU_TRANSFORM_3D`
  declarations in the final draw calls; no C1 `GU_TRANSFORM_2D`;
- selected 24/20/20/16-byte stride in producer, completion checks, cache
  ranges and consumer pointer arithmetic;
- unsigned UV versus signed XYZ interpretation, finite/range rejection and no
  saturating conversion;
- XYZ scale installation, restart reinstallation and identity restoration;
- distinct stream version/RID and no PSP-1000 marker/profile;
- OFF retains the RID30 native 24-byte producer/consumer path.

Only after the full M0 staircase may C1 be called a performance candidate.
This document does not authorize deployment of any C1 build.

# TH07 PSP ME / GE Direction Freeze — 2026-08-27

This file records the user-approved direction. It is a planning freeze, not
new ownership evidence and not permission to alter the frozen audio source.

> Historical planning record: the "current integration boundary" below was
> current on 2026-08-27 and has since been superseded. The active release
> direction is one runtime-routed EBOOT, with ARK-5 as the only supported CFW
> and `Use Extra Memory = Max` required on PSP-2000/3000/Go. See
> [README.md](../README.md), [KNOWN_ISSUES.md](KNOWN_ISSUES.md), and
> [CHANGELOG.md](../CHANGELOG.md).

## ME main line: MIST coexistence

- Keep Sony T2 alive and continue with MIST.
- Wait for m-c/d's primary-source clarification before assigning ownership in
  Slim/model-3 upper ME eDRAM.
- Use only an upper extent whose ownership and safety have been established.
- A contiguous extent large enough for the existing 393216-byte BGM ring may
  be used only after that ownership boundary is established.
- Physical 4 MiB presence must never be reported as 4 MiB freely usable.

## ME research branch: custom-core

- Treat `psp-media-engine-custom-core` as a separate backend/research branch.
- Its reset/custom-handler path replaces the Sony T2 runtime; it is not a
  small MIST allocator-policy change.
- A custom runtime may make a deliberate 4 MiB layout possible, but it must be
  proven in standalone tests before any TH07 integration.
- If m-c/d confirms that MIST coexistence cannot safely lease a sufficiently
  large contiguous upper extent, raise the priority of this branch.

## GE frozen result

- PSP-2000+ GE upper expansion verified on real PSP-3000 hardware:
  `0x04200000..0x043fffff` (2048 KiB total).
- This is a strong future candidate for portraits, backgrounds, and
  texture/surface cache storage.
- Prior verification does not by itself grant a current TH07 runtime owner;
  integration still needs an explicit ownership/lifecycle design.

## Current integration boundary

- SHIKIGAMI integration is observation-only.
- No ME ownership experiment, allocator change, pattern write, FULL POST,
  launcher, or self-update is part of the current TH07 observer build.

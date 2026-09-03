# PMD PSP ME-local eDRAM audit

Date: 2026-08-27 JST

> Historical audit: this document records the evidence available on
> 2026-08-27. It is not a support matrix or a description of the current
> unified release candidate. Current requirements are documented in
> [README.md](../README.md) and [ARK5_HIGH_MEMORY.md](ARK5_HIGH_MEMORY.md):
> ARK-5 only, with `Use Extra Memory = Max` on PSP-2000/3000/Go.

Repository: `https://github.com/kan8223-dotcom/pmd-psp`

Frozen application commit:
`1fab562c66f432cc3311bf6c05b4b917c35413d6`

Pinned custom-core fork commit:
`35d1925f2f76d1781420becca9a932f6b7329823`

## Executed local range

The application assigns its ME-local render pointer directly to `0x00000400`.
Each host producer supplies 512 stereo `short` samples, so the executed local
interval is exactly `[0x00000400, 0x00000c00)`, or 2 KiB. The ME renderer may
modify that block in place, then `meCoreMemcpy` copies it to caller-owned Main
RAM. WAV export bypasses this eDRAM path.

Primary source:

- https://github.com/kan8223-dotcom/pmd-psp/blob/1fab562c66f432cc3311bf6c05b4b917c35413d6/main.c#L1281-L1288
- https://github.com/kan8223-dotcom/pmd-psp/blob/1fab562c66f432cc3311bf6c05b4b917c35413d6/main.c#L1515-L1545

There is no local-eDRAM allocation/reservation call, model-dependent data
placement, ownership arbitration, or access to `0x00200000`/`0x00300000`.
Comments discussing a wider low range are not executable evidence. This repo
does not establish a safe 2 MiB or upper-bank ownership boundary.

## Runtime and stack model

PMD calls `meLibDefaultInit()`. The pinned custom-core copies its handler to
the ME reset vector and pulses reset. Its model/table branch places the
descending stack top at local `0x00400000` for the T2 witness, otherwise
`0x00200000` for the other accepted image. It defines no stack bottom, guard,
or proven maximum depth.

Primary source:

- https://github.com/kan8223-dotcom/psp-media-engine-custom-core/blob/35d1925f2f76d1781420becca9a932f6b7329823/me-core.h#L39-L48
- https://github.com/kan8223-dotcom/psp-media-engine-custom-core/blob/35d1925f2f76d1781420becca9a932f6b7329823/me-core-custom.c#L236-L280
- https://github.com/kan8223-dotcom/pmd-psp/blob/1fab562c66f432cc3311bf6c05b4b917c35413d6/main.c#L1352-L1382

This replaces Sony's reset/control entry but continues to use mapped Sony
services such as `meCoreMemcpy`; it is a custom-core branch, not MIST/T2 task
coexistence.

## Reuse decision

Useful for a separate TH07 custom-core research backend:

- reset/table/stack bootstrap;
- uncached SC-to-ME mailbox and cache handoff;
- STOP/FLUSH lifecycle and SC fallback structure;
- the executed 2 KiB low-eDRAM staging pattern, limited to its exact range.

Not evidence for reuse:

- any larger/free ME-local extent or upper ownership boundary;
- a safe lower bound for the descending 4 MiB-top stack;
- PMD's 1 MiB PCM ring or large loop caches, which are Main-RAM allocations;
- the separate `me_driver` PRX, which is not linked into the application.

Fresh-clone reproducibility is incomplete: `.gitmodules` does not fully map
the checked-in `tiny-me`/`pmdmini` gitlinks. The frozen audit checkout remains
at `/mnt/c/Users/kan82/TH07_PMD_PSP_EDRAM_AUDIT_20260827`.

# m-c/d ME upper ownership primary-source audit

Date: 2026-08-27 JST

> Historical audit: this document preserves the evidence and decisions made on
> 2026-08-27. It does not describe the current release candidate or its support
> requirements. See [README.md](../README.md),
> [KNOWN_ISSUES.md](KNOWN_ISSUES.md), and
> [CHANGELOG.md](../CHANGELOG.md) for the current unified-EBOOT status,
> ARK-5-only support, and the PSP-2000/3000/Go
> `Use Extra Memory = Max` requirement.

## Result

The inspected m-c/d repositories establish physical/model-dependent 4 MiB
behavior under custom-core, but none establishes a contiguous upper ME-local
extent that TH07 may own while Sony T2/MIST remains active.

`psp-media-engine-custom-core` selects a 4 MiB stack top on its T2-witness
path only after installing a custom reset handler. It does not provide a stack
bottom or MIST-compatible ownership lease:

- https://github.com/mcidclan/psp-media-engine-custom-core/blob/2f0d1be8053fb9a7fe9e28c61b795c1488c63702/me-core.h#L33-L63
- https://github.com/mcidclan/psp-media-engine-custom-core/blob/2f0d1be8053fb9a7fe9e28c61b795c1488c63702/me-core-custom.c#L236-L257

`psp-media-engine-safe-task` is the relevant coexistence path. Its MIST code
uses the T2 syscall table around local `0x003ce870`, and indexed examples touch
nearby upper addresses. Those are live T2 structures, not free heap evidence:

- https://github.com/mcidclan/psp-media-engine-safe-task/blob/e8c2d8b84e5ae0970ed5602269c81d9ca657d5e9/me-stask-mist.c#L54-L70
- https://github.com/mcidclan/psp-media-engine-safe-task/blob/e8c2d8b84e5ae0970ed5602269c81d9ca657d5e9/me-stask-mist.c#L98-L115

The native allocator implementation body and its high-water boundary are not
present in the inspected sources. The available mapping comments do not prove
that upper memory is allocator-managed.

## Direction freeze

- MIST main line: ME upper ownership remains UNKNOWN pending an authoritative
  lease/boundary from the implementation author.
- custom-core: separate runtime-replacement research backend; never treat it
  as a small MIST allocator change.
- no upper write, allocator expansion, canary, sweep, or ownership promotion is
  justified by these sources.

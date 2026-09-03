# PSP-1000 E480 hardware acceptance anchor

This directory fixes the exact pre-unification PSP-1000 rollback artifact.

- Hardware verdict: PASS on a physical PSP-1000 (2026-09-03 JST).
- CFW used by this run: unconfirmed. The accepted boot log embeds neither a
  CFW name nor a version, so neither may be inferred from the log. Formal
  support after unification is limited separately to ARK-5.
- Fixed replay: `th7_udLUNA.rpy`, stages 1 through 6 visually synchronized,
  Yuyuko defeated, and Replay Select restored after completion.
- EBOOT SHA-256: `18CF0136DE1525EF6B0ECA4FCA5BC2415A0A65875D8C0D88D53A9A509A94C365`.
- Hardware log SHA-256: `00FAB988A1430A08D5F67CD76CD98AB535E4B032E7E03A15004ECA3C5DC13611`.
- Replay identity observed by the runtime: 72,308 bytes, FNV-1a64
  `DF8402D05977CFAA`.
- Enemy manifest high-water/capacity by stage:
  `69/72`, `37/64`, `24/64`, `105/108`, `17/64`, `25/64`.
- Stage 4 passed the exact retained-arena pressure point: `5404K/5404K`.
- The accepted replay epoch and the complete hardware log contain zero
  `REPLAY INVALID`, fatal, crash, exception, assertion, allocation-failure,
  exhaustion, or abort records.
- Stage 6 completed, the 1,699 KiB title cache reloaded, the replay menu became
  ready, and the process later exited cleanly.

`EBOOT.PBP` has empty ICON0, ICON1, PIC0, PIC1 and SND0 slots. It contains no
original-game image, music, replay, font, or archive data. The replay and local
font identities are recorded in the project handoff but are intentionally not
stored in this Git anchor.

This is an exact binary rollback anchor. The repository had a long-lived dirty
integration worktree when the candidate was built, so this commit does not
claim that its parent tree alone reproduces the binary.

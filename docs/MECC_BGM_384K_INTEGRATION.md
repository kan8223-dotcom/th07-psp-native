# TH07 custom-core 384 KiB BGM integration

## Frozen boundary

- Integration base: observer commit `a3e630c14816446d194760ec68d7dfb687d8e0df`.
- The dirty shared `th07-psp-native` tree is not a build or edit source.
- Sony T2/MIST and custom-core are mutually exclusive. `meLibDefaultInit()`
  resets and replaces the Sony ME handler; this profile never links or starts
  MIST and never falls back to Sony after that boundary.
- Standalone PSP-3000 proof: model/table `3/2`, exact local extent
  `0x00200000..0x0025ffff`, tested/verified `393216/393216` bytes, restore
  verified, ME stopped, normal SHUTDOWN.
- Frozen proof log SHA-256:
  `6839e443b7c9f8461e5e059c6c116365e8fb784f366cded8d0f580fb8fd75bcc`.
- Frozen standalone EBOOT SHA-256:
  `887d66ce0ec3be22d521526b193a3b26aed58306d996684f6ed29cd1f5d5d4cd`.

## Backend

Build only with:

```text
make psp3000-mecc-bgm384k-build
```

The profile requires `PSP_SHIKIGAMI=1`, rejects PSP-1000, and rejects
`PSP_EASY_MIST_AUDIO=1`. It retains `MEMSIZE=1`; the PSP-3000 must retain the
proven ARK setting `homebrew, highmem, on`.

Activation is gated before reset by PPSSPP detection, the existing ME.OFF
marker, and `kuKernelGetModel()==3`. After invoking custom-core, table 2 and a
worker READY acknowledgement are mandatory. Any failure after that boundary is
fail-closed and requires a cold reboot.

The ME worker owns:

```text
accumulator: 0x00000400..0x00000bff
BGM ring:    0x00200000..0x0025ffff (384 KiB)
stack guard: 0x003f0000..0x003fffff (64 KiB below 0x00400000)
```

The existing Memory Stick producer reads one 64 KiB block and sends a bounded
generation-tagged UPLOAD command. A priority-0x12 feeder sends bounded 2048-byte
(512 stereo frame) FETCH commands into an eight-block Main-RAM FIFO. The
priority-0x10 audio-output thread consumes only that FIFO and never waits for
ME. The existing SC SFX mixer and PSP audio output call remain unchanged.

The canonical 384 KiB static Main-RAM ring is absent in this profile. The
eight-block FIFO and bookkeeping reduce the linked `.bss` by 376064 bytes
relative to the observer baseline while relocating the complete logical ring.
PPSSPP/model/marker rejection happens before custom reset and uses a dynamically
allocated 384 KiB SC fallback ring. Runtime failure never switches an owned ME
ring to that fallback; BGM/SFX output is silenced and a fatal is published.

Generation RESET serializes with UPLOAD/FETCH. A track change can cancel a
stale command without poisoning the worker. Bounds, exact command sizes, Main
RAM physical range, generation, and cache maintenance are checked on both
sides of the mailbox.

Initialization performs a full integration self-test: six 64 KiB uploads and
192 2 KiB fetches compare every byte of the exact 384 KiB range. Ownership is
committed to telemetry only after this passes. Runtime telemetry reuses the
existing TH07 status schema and reports the exact owned base/bytes plus ME job,
fallback, timeout, and maximum-wait counters.

Shutdown order is BGM stop, producer/feeder/output join, ME STOP, and explicit
STOPPED acknowledgement with a three-second timeout. Sony T2 is not restored;
even after a successful exit, cold reboot before another ME application is a
hard requirement. TH07's same-process option restart is disabled in this
diagnostic profile.

## Proven MECC payload gate

The standalone-proven archive SHA-256 is
`34e2cc9b5975367da8e9c7987e251d3564241e9f2d641d0c7098b970e71d68fa` and its
embedded `kcall.prx` SHA-256 is
`3f35bdcea388a5d9a86b672282a35d5a66d8f4ef27fc36190bd07f2a126bab93`.
This GNU `ar` records timestamps, so a clean rebuild changes the archive-level
hash. `tools/audit_mecc_proven.py` therefore verifies the exact ordered member
payload hashes and the exact PRX hash before linking.

## Gate status

- Host protocol/source-policy tests: PASS, 24/24.
- Proven MECC archive/member/PRX audit: PASS.
- Diagnostic PSP-3000 clean rebuild: PASS and reproducible. `EBOOT.PBP`
  SHA-256 is
  `6461938bf39bd274f35c12835664b2d9bc21ec8a25a5e9e70e9c6cd571c519f6`;
  `TH07PSP.elf` SHA-256 is
  `368b9601a3b60ab23fd2d44f62495e966b6d70e82c1c4d1bbdb7e79f7dc13724`.
- Normal PSP-2000+, PSP-1000, and observer clean builds: PASS. Every
  `EBOOT.PBP` and ELF is byte-identical to the clean base commit build; the
  diagnostic changes therefore leak zero bytes into those profiles.
- PPSSPP v1.20.4 safe-skip/startup/emulator-unload: PASS. The boot log records
  `ME AUDIO OFF (PPSSPP -> SC)` followed by
  `MECC PROFILE SAFE SC RING FALLBACK`; the title/demo reaches
  `game added ready`, returns to the PPSSPP menu, and PPSSPP exits normally.
  PPSSPP's Exit-to-menu unload does not run the PSP application's normal audio
  release path, so this gate is not evidence for ME STOP or app cleanup.
- PSP-3000 TH07 ownership/audio/exit run: PASS. The frozen run reached title,
  demo stages 4 and 5, three BGM transition boundaries, normal HOME cleanup,
  ME STOPPED, UDP SHUTDOWN, and the required post-run cold power cycle with no
  fallback, timeout, underrun, or fatal.

The standalone ownership proof and the integrated in-game TH07 durability run
are both complete.

## Completed PSP-3000 result

The controlled run on 2026-08-27 used the exact diagnostic EBOOT above on PSP
model 3. Across 204 status records, every owned extent was
`384KiB@0x00200000` and every logical ring was `393216` bytes. The run reached
18,342 completed ME jobs, `FALLBACK=0`, `TIMEOUT=0`, `UND=0`, `FATAL=0`, and a
maximum observed command wait of 31,554 microseconds. BGM indices 0, 7, and 9
covered title, demo stop/restart, title return, and a second demo transition.

The Memory Stick log records `ME AUDIO INIT R2`, `ME AUDIO ON MAP2`,
`MECC BGM 384K ON (LOCAL EDRAM)`, zero audio underruns, then
`MECC BGM STOPPED`, `SHIKIGAMI TH07 STOPPED`, and `main exited`. The receiver's
last record is a normal `[SHIKIGAMI PSP SHUTDOWN]` at PSP uptime 230,048 ms.

- Frozen telemetry SHA-256:
  `31f3863b6f9d6276a80d41e2e472ad5f75df8892a90a0972a2f542fb119a8edb`
- Frozen `TH07PSP_BOOT.LOG` SHA-256:
  `ffa1686239cee7a15bbc7d3e00c4f38ba2ddadd59b86106bc58f8aad4c52f5f3`
- Receiver stderr: empty.

After SHUTDOWN, the operator completed the required full power-off/power-on.
The observer EBOOT, score, ARK setting, and prior logs were then restored to
their exact pre-run hashes. No temporary/backup file remains on the Memory
Stick, and its free-space count returned exactly to the pre-run value.

## Re-run procedure

Start the passive receiver on the configured host before booting the PSP:

```text
mkdir -p artifacts/real-hardware
python3 tools/shikigami_th07_receiver.py --bind 192.168.11.3 \
  --log artifacts/real-hardware/TH07_MECC_BGM_384K_REALHW.log
```

While the Memory Stick is mounted, install only
`artifacts/diagnostic/EBOOT.PBP` over the EBOOT in the existing TH07 directory;
preserve all game data, configuration, and saves. Verify the installed file,
not just the PC source, has SHA-256
`6461938bf39bd274f35c12835664b2d9bc21ec8a25a5e9e70e9c6cd571c519f6`.
The wire `profile=1` and `build=0x20260827` values are shared with the normal
observer and do not identify this EBOOT by themselves.

Before boot, confirm both safety-disable markers are absent:

```text
<TH07 game directory>/TH07PSP_ME.OFF
ms0:/PSP/SYSTEM/ppsspp.ini
```

If either file was deliberately retained, rename it recoverably for this test
instead of deleting it. Otherwise the diagnostic will correctly stay on the SC
fallback and cannot prove ME ownership. Retain the proven ARK
`homebrew, highmem, on` setting. Exit USB mode, completely power the PSP off,
then cold power it on; suspend or an in-process restart is not sufficient.

Exercise title BGM, demo/game entry, several BGM changes and scene transitions,
then exit the application normally. A pass requires all of the following:

- the installed EBOOT still matches the diagnostic SHA above, and identity
  reports PSP model `3`, `profile=1`, and `build=0x20260827`;
- status consistently reports `ME_UPPER=384KiB@0x00200000` and
  `RING=393216`;
- `ME_PERF` jobs advance while `FALLBACK=0`, `TIMEOUT=0`, and `FATAL=0`;
- the Memory Stick boot log contains `MECC BGM 384K ON (LOCAL EDRAM)` and ends
  the audio lifecycle with `MECC BGM STOPPED`;
- the receiver records `[SHIKIGAMI PSP SHUTDOWN]`, rather than only a heartbeat
  timeout.

Any mismatch, silence, stall, suspend event, timeout, or fatal is a failed run.
No telemetry is also a failed run: initialization can fail before SHIKIGAMI
starts, so preserve the overwritten `ms0:/TH07PSP_BOOT.LOG` immediately and
inspect it even when the receiver saw nothing. Absence of
`MECC BGM 384K ON (LOCAL EDRAM)` is not a pass.
After either pass or failure, completely power the PSP off before launching any
other ME application. A successful UDP SHUTDOWN is not evidence that a later
cold power cycle has already occurred.

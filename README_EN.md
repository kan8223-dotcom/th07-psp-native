# th07-psp-native

[日本語](README.md) | **English**

> [!IMPORTANT]
> **The current `v1.0.0-rc1` build is the final tester/pre-release, not the final stable release.**
> Download it from [GitHub Releases](https://github.com/kan8223-dotcom/th07-psp-native/releases).
> One universal EBOOT (initial SHA-256 `822E0A4C43AC84509A25AF16D921B0BB9BCB1C4597DCDBB315E9583D5E92FAD4`) automatically selects the
> PSP-1000 profile or the PSP-2000/3000/Go profile at startup. ARK-5 is required on every model;
> PSP-2000/3000/Go also require `Use Extra Memory = Max`.
>
> The initial EBOOT has fully transparent XMB icon and background slots. Only after the first launch
> validates the user's own `th07.dat` and `thbgm.dat` does the PSP generate the icon and background
> locally. The generated EBOOT contains images derived from the original game and must not be shared
> or redistributed.

An unofficial native PSP port of *Touhou Youyoumu ~ Perfect Cherry Blossom* 1.00b (Touhou 7).
It runs ported game code on the PSP; it does not emulate the Windows executable.

> [!WARNING]
> This repository and its release package contain none of the original Touhou 7 data or images derived
> from it. You must provide the complete installation folder from your own copy of Touhou 7 1.00b.

## Features

- Based on [some100/th07's portable branch](https://github.com/some100/th07/tree/portable).
- One EBOOT detects the PSP model at startup and automatically selects the PSP-1000 32 MiB profile or
  the PSP-2000/3000/Go 64 MiB profile. There is no model-specific EBOOT to choose.
- Rendering uses the PSP Graphics Engine through PSPSDK libGU/libGUM, with VFPU-enabled libGUM and
  PSP-specific numerical paths.
- Real hardware uses m-c/d's
  [Media Engine Custom Core](https://github.com/mcidclan/psp-media-engine-custom-core) for PCM mixing,
  with a safe fallback to the main CPU.
- The PSP-1000 audio profile uses the MIST method from M-cid (m-c/d)'s
  [PSP Media Engine Safe Task](https://github.com/mcidclan/psp-media-engine-safe-task). Moving the BGM
  ring into ME-local eDRAM recovers a net 393,088 bytes (383.875 KiB) of Main RAM.
- The original 640x480 logical image is displayed at 480x272 in either 4:3-fit or full-stretch mode.
- Settings, scores, and replays are written beside the EBOOT, never into the copied original folder.
- The formal ZIP carries both the full Noto font for PSP-2000/3000/Go and a 1,190-codepoint subset
  derived from the same OFL Noto font for PSP-1000.

## Formal unified release status

The final tester/pre-release universal EBOOT is now published through GitHub Releases without Beta or
tester branding in its XMB title. Promotion to the final stable release will be decided separately from
physical-device acceptance results. A PC or PPSSPP result alone is not hardware acceptance.

The old statement that PSP-1000 replay synchronization was unverified was superseded by a physical
hardware acceptance run on 2026-09-03. EBOOT SHA-256
`18cf0136de1525ef6b0eca4fca5bc2415a0a65875d8c0d88d53a9a509a94c365` played the fixed external
Lunatic replay `th7_udLUNA.rpy` in sync from stage 1 through stage 6, Yuyuko's defeat, and the return to
Replay Select on a physical PSP-1000. That boot log records neither the CFW name nor its version, so the
CFW used for this run remains unconfirmed; future formal support is limited to ARK-5. Exact replay,
EBOOT, and log hashes are fixed in the
[changelog](CHANGELOG.md) and [hardware anchor](release-anchors/psp1000-e480-hw-pass-20260903/README.md).

This result applies to that fixed replay. It is not a blanket guarantee for every replay, shot type, or
difficulty. On PSP-1000, a replay that fails the identity or reserved-capacity contract is rejected with
`REPLAY INVALID` instead of silently omitting enemies and desynchronizing.

> [!IMPORTANT]
> ARK-5 is the only supported CFW. On PSP-2000, PSP-3000, and PSP Go, set ARK-5
> `Use Extra Memory` to `Max` before launch. `Default`, `Off`, and an explicit
> `always, highmem, off` rule are unsupported and may stop the game before startup. PSP-1000 has no
> extra Main RAM, so the Max setting does not apply to that model, but ARK-5 is still required.

> [!TIP]
> For stability in heavy scenes, press SELECT during play to enable fixed-30 mode. Rendering changes to
> 30 fps while game logic, input, BGM, and sound effects retain their normal speed.

## Installation

You need:

- a PSP-1000, PSP-2000, PSP-3000, or PSP Go that runs homebrew through ARK-5;
- the universal final tester/pre-release ZIP published on GitHub Releases;
- your own complete Windows installation of Touhou 7 1.00b; and
- a USB connection or card reader.

Steps:

Install the current final tester/pre-release universal build as follows.

1. Install ARK-5. On PSP-2000/3000/Go, set `Use Extra Memory` for this application to `Max`.
2. Extract the release ZIP.
3. Copy its `TH07PSP` folder into `PSP/GAME/` on the Memory Stick or PSP Go internal storage.
4. Copy your whole Touhou 7 installation folder into `PSP/GAME/TH07PSP/`.
5. Rename the copied original folder to the ASCII name `th7`. If `th7` already exists, copy the
   **contents** of the original folder directly into it, not the enclosing original folder.
6. Start `東方妖々夢 ～ Perfect Cherry Blossom.` from XMB.

The finished layout is:

```text
PSP/GAME/TH07PSP/
├── EBOOT.PBP
├── NotoSansJP-Regular.ttf
├── msgothic-subset.ttf
├── README.md
├── CREDITS.md
├── LICENSE
├── licenses/
└── th7/                         <- the original folder copied by you
    ├── th07.dat
    ├── thbgm.dat
    ├── 東方妖々夢.exe           <- its Japanese name does not need to change
    └── other original files
```

Both font files are required. The PSP-1000 payload uses the 264,288-byte
`msgothic-subset.ttf`; the PSP-2000/3000/Go payload uses the 4,491,696-byte
`NotoSansJP-Regular.ttf`. The former is only a runtime-compatibility filename. Its metadata and
outlines are derived from OFL-licensed Noto Sans CJK JP 2.004, not Microsoft MS Gothic. Do not replace
it with or redistribute a private subset made from a Windows font. The generation, hashes, and license
contract are recorded in the [formal-release font specification](docs/PSP_RELEASE_FONTS.md).

The EBOOT checks the headers and expected 1.00b sizes of `th07.dat` and `thbgm.dat`: 23,829,135 bytes
and 444,516,656 bytes, respectively. Missing data or a different game version is rejected instead of
being partially loaded.

> [!WARNING]
> Do not put the original game folder itself inside `th7`. A one-level-too-deep layout such as
> `TH07PSP/th7/東方妖々夢/th07.dat` lets the unified launcher and model-specific runtime start, but the
> runtime then logs `original TH07 1.00b data not found` and exits. Place `th07.dat` and `thbgm.dat`
> directly in `TH07PSP/th7/`.

### First XMB appearance and local files

The distributed EBOOT initially contains only tool-generated, fully transparent neutral placeholders
in ICON0 and PIC1, so XMB shows neither a custom thumbnail nor a background. Only after the first launch validates
the user's own `th07.dat` and `thbgm.dat` does it generate ICON0 and PIC1 locally on that PSP. The images
appear the next time the entry is shown in XMB. The release contains the generator, not generated image
binaries. Safe local update and the next XMB appearance have passed on physical hardware. This is still
a final tester/pre-release result, not a declaration of the final stable release.

After this local transformation, `EBOOT.PBP` contains images derived from the user's original data.
Never redistribute that EBOOT, the generated images, or the PSP-1000 `title01.psp1000.cache`, and do not
attach them to a bug report. `TH07RUNTIME.PBP` is the automatically selected model runtime and can be
deleted; the launcher recreates it when required.

Settings, scores, and new replays are created beside the EBOOT, including `th07.cfg`, `score.dat`, and
the `replay/` directory.

### Alternative original-data location

The recommended location is `PSP/GAME/TH07PSP/th7/`. You may instead put the complete original folder
directly under `ms0:/`, or under `ef0:/` on PSP Go. The launcher and model-specific runtime each search,
non-recursively, the launch-device root and its immediate child directories, plus `th7` directories in
sibling applications under `PSP/GAME/`. Every candidate directory must contain the valid `th07.dat`
and `thbgm.dat` pair **directly**. A further nested original-game directory is not searched. All PSP
models use the same rules.

### If the game does not start

Check these items in order:

1. The CFW is ARK-5.
2. On PSP-2000/3000/Go, `Use Extra Memory` is `Max`.
3. `EBOOT.PBP` is at `PSP/GAME/TH07PSP/EBOOT.PBP`.
4. Both `NotoSansJP-Regular.ttf` and `msgothic-subset.ttf` are still beside the EBOOT and unmodified.
5. `PSP/GAME/TH07PSP/th7/th07.dat` and `thbgm.dat` both exist **directly** there, not inside another
   original-game directory.
6. The original game is version 1.00b.

If `TH07PSP_BOOT.LOG` says `original TH07 1.00b data not found`, check item 5 first. This layout error
can terminate only the runtime even after the unified launcher has successfully selected and started it.

ARK-5 settings may be lost after changing a Memory Stick or reinstalling ARK. Recheck `Max` even if the
same PSP launched the game previously. See the [ARK-5 setup guide](docs/ARK5_HIGH_MEMORY.md) for safely
removing a conflicting `always, highmem, off` rule. Never overwrite your complete `SETTINGS.TXT` with
the packaged snippet.

The port writes `TH07PSP_BOOT.LOG` to the root of the launch device. Copy it to a PC before launching
the game again, because each launch overwrites it. A bug report should include the log, exact PSP model,
ARK-5 version, game mode/difficulty/stage/action, reproduction steps, and EBOOT SHA-256. Never attach
original data or locally generated derivatives.

## Controls

| PSP control | Action |
|---|---|
| D-pad / analog nub | Move; select menu items |
| X | Shoot; confirm; advance dialogue |
| Circle | Bomb; cancel |
| Square or L/R | Focus / slow movement |
| Triangle | Skip dialogue |
| START | Pause |
| SELECT | Toggle normal 60 fps rendering and fixed-30 mode |
| HOME / PS button | Open the PSP exit or suspend menu |

The original game's windowed option is treated as `4:3 FIT` (362x272 with pillarboxes). Its fullscreen
option is treated as `FULL STRETCH` (480x272).

## Current limitations

- PPSSPP success does not prove the same path on hardware. Memory Stick I/O, ARK-5, the Media Engine,
  and the 32/64 MiB memory models can behave differently.
- Heavy bullet/effect scenes and later 3D stages can still slow down. This is the original-style
  slowdown that advances the game more slowly; use fixed-30 mode when preferred.
- Back up saves and arbitrary external replays before an update. The 2026-09-03 synchronization result
  is scoped to the fixed Lunatic replay described above.
- Music Room and a small number of rendering fixes still have paths that need representative-hardware
  rechecks after PPSSPP acceptance.
- The bundled Noto-derived `msgothic-subset.ttf` passes the PC audit, but a PSP-1000 stage-4 and fixed
  stage-1-through-6 Lunatic run using this exact hash remains a gate before promotion to final stable.

See [Known issues](docs/KNOWN_ISSUES.md) and the [changelog](CHANGELOG.md) for detailed status.

## Not included

Neither the repository nor a release package includes:

- the original Touhou 7 executable, DAT archives, images, music, sound effects, or other game data;
- generated XMB images or an EBOOT containing those images;
- original or development replays (`.rpy`);
- user settings, scores, logs, or save data;
- Microsoft fonts (`msgothic-subset.ttf` in the package is an OFL Noto Sans CJK JP derivative despite
  its compatibility filename); or
- diagnostic EBOOTs with autoplay, infinite lives, forced MAX power, or similar test features.

## Unofficial project

This project is not an official product of Team Shanghai Alice or ZUN, and it is not endorsed,
supported, or guaranteed by them. Do not contact Team Shanghai Alice, ZUN, some100, GensokyoClub,
m-c/d, PSPDEV, PPSSPP, or authors and maintainers of referenced projects for support with this port.

Questions and bug reports about the port belong in this repository's Issues. This project does not
provide the original game, CFW, or help with obtaining or sharing copyrighted game data.

## Building from source

Install PSPDEV/PSPSDK at `/usr/local/pspdev` and make CMake available. The bundled MECC source is built
from source. The formal archive's reproducible font audit additionally requires `fontTools==4.62.1`.

```sh
make -j"$(nproc)" all
make psp1000-build
make psp2000plus-build
make release
```

`make release` combines the two fixed runtime anchors with a clean-built unified launcher and creates
one local candidate archive, `dist/th07-psp-native-v1.0.0.zip`. It does not create a GitHub tag or
Release. The release audit checks that the initial XMB slots exactly match the transparent neutral placeholders, both
model runtimes have the expected hashes, and no original data, user data, diagnostic EBOOT, or derived
image entered the archive. It also regenerates and checks both Noto fonts, including the pinned
source/license/output hashes, exact 1,190-codepoint cmap, and kern-only layout contract. Profile-specific
targets remain for development and regression checks; no model-specific ZIP is distributed.

A direct-stage diagnostic build is available only when explicitly requested and must not be distributed:

```sh
make PSP_DIRECT_GAME=1 PSP_DIRECT_STAGE=5 -j"$(nproc)"
```

## Credits and license

Implementation sources and acknowledgements, including the upstream decompilation, TH06 PSP port,
MECC, and MIST, are listed in [CREDITS.md](CREDITS.md).

The main project follows the upstream some100/th07 license and is released under CC0 1.0 Universal;
see [LICENSE](LICENSE). Bundled third-party components and fonts retain their own licenses. Original
Touhou 7 game data is not covered by this license.

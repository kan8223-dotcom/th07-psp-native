# th07-psp-native

[日本語](README.md) | **English**

An unofficial, work-in-progress native PSP port of *Touhou Youyoumu ~ Perfect Cherry Blossom* 1.00b
(Touhou 7). It runs ported game code on the PSP; it does not emulate the Windows executable.

> [!WARNING]
> This repository and its release EBOOTs contain **none of the original Touhou 7 game data**.
> You must provide the complete installation folder from your own copy of the Windows version of
> Touhou 7, updated to version 1.00b.

## Before you install

The current release is v0.1.4-beta and is intended for testing. There are two separate downloads:

- **PSP-1000:** use the archive whose name ends in `-psp1000.zip`. This is a special 32 MiB tester
  build with reduced object limits.
- **PSP-2000, PSP-3000, and PSP Go:** use the archive whose name ends in `-psp2000plus.zip`.
  This is the 64 MiB build.

Do not mix files from the two packages. In particular, do not copy an EBOOT intended for a different
PSP model over the one in your selected package.

> [!IMPORTANT]
> For more stable play, press **SELECT** after starting the game to enable fixed-30 mode. This reduces
> rendering load while game logic, input, BGM, and sound effects continue at their normal speed.
> Press SELECT again to return to the normal 60 fps rendering mode.

> [!WARNING]
> The PSP-1000 build does not currently provide reliable replay synchronization. Do not use that build
> to verify replays, scores, or compatibility with the original game. See
> [Known limitations](#known-limitations).

## What you need

- A PSP-1000, PSP-2000, PSP-3000, or PSP Go that can run homebrew through CFW.
- The package for your PSP model from the
  [Releases page](https://github.com/kan8223-dotcom/th07-psp-native/releases).
- Your own installed copy of the Windows version of Touhou 7, updated to **1.00b**.
- A USB connection or card reader with which to copy files to PSP storage.

You do not need to unpack either DAT archive or rename individual Japanese files.

## Installation

1. Extract the release ZIP on your computer.
2. Copy the extracted `TH07PSP` folder into `PSP/GAME/` on your Memory Stick or PSP Go storage.
3. Locate the Touhou 7 installation folder on your computer.
4. Copy that **whole folder** into the new `PSP/GAME/TH07PSP/` folder.
5. Rename the copied original-game folder to the ASCII name `th7`.
6. Safely disconnect the PSP, then start `Touhou 7 PSP-1000 Beta` or
   `Touhou 7 PSP-2000+ Beta` from the XMB game list.

The finished layout should look like this:

```text
PSP/GAME/TH07PSP/
├── EBOOT.PBP
├── NotoSansJP-Regular.ttf
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

The EBOOT checks the headers and the expected 1.00b file sizes of `th07.dat` and `thbgm.dat`.
Missing data or another game version is rejected instead of being partially loaded.

### Alternative location for the original game

The recommended and simplest location is `PSP/GAME/TH07PSP/th7/`. If you do not want the original
folder inside `TH07PSP`, you may instead place it directly under `ms0:/`, or under `ef0:/` on PSP Go.
The port checks the device root and folders one level below it for a matching `th07.dat` and
`thbgm.dat` pair.

### Files created by the port

Settings, scores, and new replays are stored beside `EBOOT.PBP`, not in your copied original-game
folder. These include `th07.cfg`, `score.dat`, and the `replay/` directory.

On its first title-screen load, the PSP-1000 build creates an approximately 1.4 MiB file named
`title01.psp1000.cache` beside the EBOOT. It is a 16-bit cache derived from your `th07.dat` and is used
when returning to the title. It can be deleted and will be rebuilt on the next launch. Because it is
derived from the original game, do not redistribute it or attach it to a bug report.

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

## Known limitations

- This is a beta. Save-data and replay compatibility or integrity is not guaranteed. Back up important
  files before testing a new build.
- PPSSPP is useful for testing, but a successful PPSSPP run does not prove that the same path works on
  real PSP hardware. Memory Stick I/O, CFW, Media Engine audio, and memory limits can behave differently.
- The PSP-1000 build lowers the maximum numbers of enemies, bullets, items, and effects to fit 32 MiB
  of RAM. That can change object-slot reuse and game state, so recorded replays commonly desynchronize.
  Testers have also reported desynchronization in the built-in title-screen demos.
- The PSP-1000 v0.1.4-beta Normal route has been tested on hardware from stage 1 through stage 6,
  Yuyuko, the ending, staff roll, and return to the title. Easy, Hard, Lunatic, Extra, and Phantasm
  clears have not been verified on that build.
- Heavy bullet/effect scenes and later 3D stages can still slow down. Fixed-30 mode is recommended for
  normal play.
- Music Room loading, playback, and return-to-title fixes pass current PPSSPP tests, but the reported
  slow entry, low frame rate, or return to XMB still needs confirmation on real hardware.
- The fix for a one-pixel portrait artifact at the top of the PSP display has passed PPSSPP image
  comparisons but still needs real-hardware confirmation.
- A tester reported that a Lunatic replay could finish through Yuyuko but fail to return to Replay
  Select. The affected PSP model and 32/64 MiB package have not yet been identified.

More technical status is recorded in the [changelog](CHANGELOG.md) and the
[detailed known-issues document](docs/KNOWN_ISSUES.md), which are currently written in Japanese.

## If the game does not start

Check these items in order:

1. `EBOOT.PBP` is at `PSP/GAME/TH07PSP/EBOOT.PBP`.
2. `NotoSansJP-Regular.ttf` is still beside the EBOOT.
3. `PSP/GAME/TH07PSP/th7/th07.dat` and `thbgm.dat` both exist.
4. Your original game is version 1.00b.
5. You installed the package intended for your PSP model.

If the game still returns to XMB or fails during a stage transition, preserve the boot log before
launching the game again.

## Logs and bug reports

The port writes `TH07PSP_BOOT.LOG` to the root of the device from which the EBOOT was launched:

- Memory Stick launch: `ms0:/TH07PSP_BOOT.LOG`
- PSP Go internal-storage launch: `ef0:/TH07PSP_BOOT.LOG`

The log is overwritten on each launch. After reproducing a problem, copy it to your computer **before
starting the game again**.

When opening an issue, include:

- the boot log;
- your exact PSP model;
- your CFW name and version;
- whether you used the PSP-1000 or PSP-2000+ package;
- the game mode, difficulty, stage, and exact action after which the problem appeared; and
- clear reproduction steps, if known.

Report problems through this repository's
[Issues page](https://github.com/kan8223-dotcom/th07-psp-native/issues). Do not attach original DAT,
music, executable, image, sound, or derived cache files. Replays are user data and are not part of the
release package; attach one for diagnosis only when a maintainer requests it.

## Not included

Neither the repository nor a release package includes:

- the original Touhou 7 executable, DAT archives, images, music, sound effects, or other game data;
- original or development replays (`.rpy`);
- user settings, scores, logs, or save data;
- Microsoft fonts; or
- diagnostic EBOOTs with autoplay, infinite lives, forced MAX power, or similar test features.

## Unofficial project

This project is not an official product of Team Shanghai Alice or ZUN, and it is not endorsed,
supported, or guaranteed by them. Please do not contact Team Shanghai Alice, ZUN, some100,
GensokyoClub, m-c/d, PSPDEV, PPSSPP, or the authors and maintainers of referenced projects for support
with this port.

Questions and bug reports about the port belong in this repository's Issues. This project does not
provide the original game, CFW, or help with obtaining or sharing copyrighted game data.

## Building from source

For developers, install PSPDEV/PSPSDK at `/usr/local/pspdev` and make CMake available. The bundled MECC
source is built automatically on the first `make`.

```sh
make -j"$(nproc)" all
make psp1000-build
make release
```

`make release` clean-builds both hardware profiles and creates the two model-specific ZIP files. A
direct-stage diagnostic build is available only when explicitly requested and must not be distributed:

```sh
make PSP_DIRECT_GAME=1 PSP_DIRECT_STAGE=5 -j"$(nproc)"
```

## Credits and license

Implementation sources and acknowledgements, including the upstream decompilation, the TH06 PSP port,
and MECC, are listed in [CREDITS.md](CREDITS.md).

The main project follows the upstream some100/th07 license and is released under CC0 1.0 Universal;
see [LICENSE](LICENSE). Bundled third-party components and fonts retain their own licenses. Original
Touhou 7 game data is not covered by this license.

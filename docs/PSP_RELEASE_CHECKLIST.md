# TH07 PSP release regression checklist

This list records failures already observed during bring-up.  A normal EBOOT
is not a release candidate until every item passes without the debug route.

## Build boundary

- `make` boots the title; it must not jump directly to a stage.
- No auto-fire, infinite lives or forced MAX power is present.
- `make PSP_DIRECT_GAME=1` remains a separate diagnostic build.
- `PSP_PERF_DIAG=1` may expose all six Practice stages for testing, but a
  release build must retain the original clear-count gate and must not alter
  `clrd`, `pscr` or `score.dat` to unlock them.
- The EBOOT and deployed copy have the same SHA-256 hash.

## Title, text and transitions

- The title background, title logo, Reimu, all eight menu items, help text,
  version and copyright line are visible.  Check this on first boot, after a
  manual demo abort, and after returning from Music Room; a 60 fps log alone
  is not a pass.
- Leave the title untouched until its attract-mode replay starts.  The title
  surface/cache must be released, the demo must load without a crash, and the
  game must eventually return to the title.  This is the same transition class
  that failed during TH06 PSP bring-up.  The manual-abort chain break is fixed
  and passes PPSSPP, but still needs hardware verification.  Test automatic
  completion and button-triggered abort as two separate paths, twice each; a
  PPSSPP-only pass is not sufficient.
- The "白銀の春" stage logo and all title strings are present.
- Character select reaches gameplay without a long frozen interval.
- Dialogue is Japanese, is not blank/garbled, and is not replaced by the
  prewarm string `さむ～`.
- Entering dialogue does not freeze the game.

## Rendering

- The HUD is not a tile grid.
- The stage background remains correct through dialogue, boss entry and
  stage transitions.
- The top edge and game frame do not flicker.
- Both `4:3 FIT` (362x272) and `FULL STRETCH` (480x272) have clean edges;
  pillarboxes never retain pixels from an earlier frame.
- On stage 4, measure the first cloud-top 3D section and the Prismriver
  netherworld gate separately.  The latter previously stayed near 30 fps.
- On stage 5, measure the 3D stair section even before a dense pattern begins.
- At spell start, verify that omitting the PSP low-priority 3D overlap layers
  does not expose black gaps or remove the high-priority base scene.
- Verify title, Result, Music Room, dialogue, spell cut-ins and all six stages
  with the RGB565 display buffers; look for banding, wrong alpha, retained
  pillarbox pixels or a transition-only crash.

## Timing and sound

- Gameplay, BGM and SE all run at normal speed.
- Press SELECT during gameplay to enter fixed-30 mode.  Rendering must settle
  at 30 fps while simulation, timers, BGM and SE retain their normal speed;
  press SELECT again and verify rendering returns to 60 fps.  Save and play a
  replay after toggling to ensure the mode-control bit was not recorded as
  gameplay input.
- In the stage 1 boss fight, compare the steady section with the few seconds
  immediately after the boss bomb/spell starts.  Record Effect, Bullet, draw
  and texture-upload time separately instead of treating it as a general
  stage slowdown.
- There is no periodic game/audio hitch from synchronous Memory Stick logging
  or streaming I/O.  Normal builds must not write a diagnostic line every few
  seconds.
- The boot log states whether the audio path is active or has fallen back;
  BGM plus several simultaneous SE must be tested together.
- `se_power0.wav` is heard only on its actual event and never drones during
  Cirno's appearance or dialogue.
- the cherry-border sound stops when the effect ends.
- Pausing, HOME suspend/resume and returning to the title do not leave a
  looping SE or mute the next scene.
- Enter Music Room through the normal title menu.  Its background, character,
  ten visible track titles and initial comment are present; it must not stay
  near the previously reported 19 fps.
- A local `music_bg.rgb565` cache generated from the user's own `th07.dat`
  reaches `music raw cache loaded` and `music added ready` without entering
  the removed PSP JPEG hardware decoder.  The derived cache must never enter
  a source commit or redistributable release archive.
- Select a different track.  BGM startup is dispatched before the progressive
  eight-line comment redraw, no old/new comment lines are mixed, and stable
  rendering returns to 60 fps.
- Return from Music Room.  The process must remain alive, the title must be
  fully redrawn, and returning to XMB is a failure.

## Full route

- On stage 4, hold the dialogue-skip button through the Prismriver encounter dialogue. The three
  nested boss spawns must retain separate enemy slots, and the first attack must begin after dialogue.
- Start from the title, clear stages 1 through 6, enter the ending and return
  to the title.
- At the stage 5 -> 6 boundary, the log must contain `replay compact stage 5`
  followed by stage 6 `game added gui ready` and `game added ready`; returning
  to XMB is a failure.
- The staff-roll white hold, fade, background and credits advance at normal
  speed; it must not look permanently white or take minutes per line.
- `th07.cfg`, `score.dat` and `replay/` are written beside EBOOT, never into
  the copied original `th7` directory.

## Persistence and installation

- Launch with a whole copied original folder named `th7`; do not require the
  user to select or rename Japanese source files one by one.
- Reject missing or incorrect 1.00b `th07.dat` / `thbgm.dat` before entering
  the game instead of reaching a partial-load crash.
- Save settings and score, quit, relaunch and verify both are loaded.
- Save a replay, quit, relaunch and play it back far enough to detect timing,
  input, RNG or floating-point divergence.
- The release archive contains only EBOOT, documentation, license files and a
  redistributable font.  It must contain no original DAT/BGM, user save or
  replay data, Microsoft font, or diagnostic EBOOT.

## Hardware gates inherited from TH06 PSP

- PPSSPP is only the first gate.  Repeat boot, audio overlap, stage transition,
  save/reload, suspend/resume, ending and title return on a PSP-3000-class real
  device and at least one independent CFW/Memory Stick setup.
- PSP-1000 has only the 32 MiB memory model.  It remains explicitly unsupported
  until peak memory through stage transitions, bosses and ending is measured
  and brought below that limit; a PSP-2000/3000 success is not evidence of
  PSP-1000 support.
- Record CPU work, GE wait, VBlank wait, audio fallback and I/O duration when a
  scene drops from 60 to 30 fps.  Merely using GU/GUM or enabling a VFPU thread
  flag does not prove the hot path uses VFPU or meets the frame deadline.
- Exercise HOME suspend/resume repeatedly.  Audio must resume exactly once,
  callbacks must remain registered after the first resume, and HOME exit must
  use the normal cleanup path.

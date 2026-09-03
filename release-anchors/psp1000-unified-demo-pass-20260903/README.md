# PSP-1000 unified runtime anchor (2026-09-03)

This is the 32 MiB runtime embedded in the v1.0.0-rc1 unified tester release.
It follows the hardware-accepted E480 manifest build and additionally exempts
the built-in demo from the external replay manifest, fixing the demo's return
to title. A physical PSP-1000 tester accepted startup, the built-in demo and
the fixed six-stage Lunatic replay path. No original game data is embedded.

The v1.0.0 stable tag still requires the public OFL subset-font hardware gate.
